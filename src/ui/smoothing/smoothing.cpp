#include "smoothing.h"

#include <algorithm>
#include <cmath>

float manualResponseTime(float amount) {
    amount = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.6f;
    return 0.04f * std::pow(1.5f / 0.04f, amount);
}

float manualSmoothingAmount(float seconds) {
    seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.04f, 1.5f) : manualResponseTime(.6f);
    return std::log(seconds / 0.04f) / std::log(1.5f / 0.04f);
}

// https://www.mathworks.com/help/audio/ref/audioenvelopefollower-system-object.html
// https://librosa.org/doc/0.11.0/generated/librosa.util.peak_pick.html
// https://www.itl.nist.gov/div898/handbook/pmd/section1/pmd141.htm
// https://www.dsprelated.com/freebooks/filters/Time_Constant_One_Pole.html
float AudioResponseTracker::update(const SmoothingSignalFeatures& features, bool abruptAttack) {
    const double time = features.audioTimeSeconds;
    if (!std::isfinite(time)) return static_cast<float>(response_);
    if (time == previousTime_) return static_cast<float>(response_);
    if (time < previousTime_) *this = {};
    const double dt = std::isfinite(previousTime_) ? time - previousTime_ : 0.0;
    const double level = std::isfinite(features.frameLevelDb) ? std::clamp(static_cast<double>(features.frameLevelDb), -140.0, 24.0) : -140.0;
    levels_.emplace_back(time, level);
    modulationLevels_.emplace_back(time, level);
    while (modulationLevels_.size() > 2 && modulationLevels_[1].first < time - 1.0)
        modulationLevels_.pop_front();
    while (levels_.size() > 2 && levels_[1].first < time - 0.15) levels_.pop_front();
    double sumT = 0.0, sumL = 0.0, sumTT = 0.0, sumTL = 0.0, sumLL = 0.0, total = 0.0;
    double variation = 0.0, firstLevel = level;
    for (std::size_t i = 1; i < levels_.size(); ++i) {
        const double start = std::max(levels_[i-1].first, time - .15);
        const double end = levels_[i].first;
        const double a = start - time, b = end - time, width = b - a;
        if (width <= 0.0) continue;
        const double first = std::lerp(levels_[i-1].second, levels_[i].second,
            (start - levels_[i-1].first) / (end - levels_[i-1].first));
        const double last = levels_[i].second;
        if (total == 0.0) firstLevel = first;
        sumT += width*(a+b)*.5;
        sumL += width*(first+last)*.5;
        sumTT += width*(a*a+a*b+b*b)/3.0;
        sumTL += width*((2*a+b)*first+(a+2*b)*last)/6.0;
        sumLL += width*(first*first+first*last+last*last)/3.0;
        total += width;
        variation += std::abs(last-first);
    }
    double slope = 0.0, confidence = 0.0;
    if (total >= .06 - 1e-9) {
        const double varianceT = sumTT - sumT*sumT/total;
        const double varianceL = sumLL - sumL*sumL/total;
        const double covariance = sumTL - sumT*sumL/total;
        if (varianceT > 1e-12 && varianceL > 1e-8) {
            slope = covariance / varianceT;
            confidence = std::clamp(covariance*covariance/(varianceT*varianceL), 0.0, 1.0);
        }
    }
    const double monotonicity = variation > 0.0 ? std::abs(level - firstLevel) / variation : 0.0;
    const auto smoothWeight = [](double value, double start, double end) {
        const double fraction = std::clamp((value - start) / (end - start), 0.0, 1.0);
        return fraction * fraction * (3.0 - 2.0 * fraction);
    };
    const double trendWeight = smoothWeight(confidence, .7, .9) * smoothWeight(monotonicity, .9, .99);
    const bool trend = trendWeight > 0.0;
    const double noise = std::isfinite(features.spectralFlatness) ? std::clamp(static_cast<double>(features.spectralFlatness), 0.0, 1.0) : 0.0;
    double desired = std::lerp(.4, .75, noise);
    constexpr double response95 = 2.995732273553991;
    constexpr double decibelsPerNeper = 8.685889638065037;
    constexpr double trackingError = .25;
    if (trend && std::abs(slope) > 3.0) {
        const double sourceTimeConstant = decibelsPerNeper / std::abs(slope);
        const double trackingRatio = trackingError / (1.0 + trackingError);
        desired = std::lerp(desired, std::clamp(response95 * sourceTimeConstant * trackingRatio, .08, desired), trendWeight);
    }
    const bool earlyAttack = abruptAttack && !previousAbruptAttack_;
    const bool confirmedAttack = features.onsetDetected && !previousOnset_;
    const bool accepted = (earlyAttack || (confirmedAttack && !earlyAttackPending_)) &&
        level > -70.0 &&
        (!std::isfinite(lastAttack_) || time - lastAttack_ >= .08 - 1e-9);
    attackDetected_ = accepted;
    if (accepted) {
        if (earlyAttack) earlyAttackPending_ = true;
        if (std::isfinite(lastAttack_)) {
            const double interval = time - lastAttack_;
            if (interval > 1.0) intervals_.clear();
            else { intervals_.push_back(interval); if (intervals_.size() > 6) intervals_.pop_front(); }
        }
        lastAttack_ = time;
        attackResponse_ = trend && slope > 3.0 ?
            std::clamp(response95 * decibelsPerNeper / slope * trackingError / (1.0 + trackingError), .06, .08) : .06;
        attackUntil_ = time + .06;
    }
    if (confirmedAttack || (!abruptAttack && features.spectralFlux <= 0.0f)) earlyAttackPending_ = false;
    previousOnset_ = features.onsetDetected;
    previousAbruptAttack_ = abruptAttack;
    if (time <= attackUntil_ + 1e-9) desired = std::min(desired, attackResponse_);
    if (intervals_.size() >= 3) {
        auto intervals = intervals_;
        std::sort(intervals.begin(), intervals.end());
        const double interval = (intervals[(intervals.size()-1)/2] + intervals[intervals.size()/2]) * .5;
        if (time - lastAttack_ < std::min(1.0, 2.0*interval) && intervals.back() < 1.5*intervals.front())
            desired = std::clamp(desired, .08, std::clamp(interval, .12, .4));
    }
    double minimumLevel = level, maximumLevel = level;
    int reversals = 0, direction = 0;
    for (std::size_t i = 1; i < modulationLevels_.size(); ++i) {
        minimumLevel = std::min(minimumLevel, modulationLevels_[i].second);
        maximumLevel = std::max(maximumLevel, modulationLevels_[i].second);
        const double difference = modulationLevels_[i].second - modulationLevels_[i - 1].second;
        const int nextDirection = difference > .05 ? 1 : difference < -.05 ? -1 : 0;
        if (nextDirection != 0) {
            if (direction != 0 && nextDirection != direction) ++reversals;
            direction = nextDirection;
        }
    }
    if (!modulationLevels_.empty() && time - modulationLevels_.front().first >= .5 - 1e-9 &&
        reversals >= 3 && maximumLevel - minimumLevel > 1.0 && maximumLevel - minimumLevel < 24.0)
        desired = std::max(desired, .22);
    if (accepted) response_ = desired;
    else response_ += -std::expm1(-dt / (desired < response_ ? .02 : .15)) * (desired - response_);
    previousTime_ = time;
    return static_cast<float>(response_);
}

void AudioResponseTracker::shiftLevel(double decibels) {
    if (!std::isfinite(decibels) || decibels == 0) return;
    for (auto& level : levels_) level.second += decibels;
    for (auto& level : modulationLevels_) level.second += decibels;
}
