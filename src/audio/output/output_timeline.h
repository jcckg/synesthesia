#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

class AudioOutputTimeline {
public:
    struct Interval {
        double time;
        double duration;
    };

    std::optional<Interval> advance(double reportedTime, double duration, bool discontinuity = false) {
        if (!std::isfinite(reportedTime) || !std::isfinite(duration) || duration <= 0) {
            reset();
            return std::nullopt;
        }
        if (!ready_ || discontinuity) {
            nextTime_ = reportedTime;
            observationElapsed_ = 0;
            candidateReady_ = false;
            ready_ = true;
        }
        observationElapsed_ += duration;
        const double error = reportedTime - nextTime_;
        const double tolerance = std::max(minimumToleranceSeconds, duration * .5);
        bool accepted = std::abs(error) <= tolerance;
        if (accepted) candidateReady_ = false;
        else {
            if (!candidateReady_ || std::abs(reportedTime - candidateNextTime_) > tolerance) {
                candidateNextTime_ = reportedTime;
                candidateElapsed_ = 0;
            } else candidateElapsed_ += candidateDuration_;
            candidateNextTime_ += duration;
            candidateDuration_ = duration;
            candidateReady_ = true;
            accepted = candidateElapsed_ >= offsetConfirmationSeconds;
        }
        double correction = 0;
        if (accepted) {
            correction = std::clamp(error * -std::expm1(-observationElapsed_ / responseSeconds),
                -duration * maximumRateAdjustment, duration * maximumRateAdjustment);
            observationElapsed_ = 0;
        }
        const Interval interval{nextTime_, duration + correction};
        nextTime_ += interval.duration;
        return interval;
    }

    void reset() { ready_ = false; }

private:
    static constexpr double minimumToleranceSeconds = .001;
    static constexpr double responseSeconds = 1;
    static constexpr double maximumRateAdjustment = .02;
    static constexpr double offsetConfirmationSeconds = .25;
    double nextTime_ = 0;
    double observationElapsed_ = 0;
    double candidateNextTime_ = 0;
    double candidateDuration_ = 0;
    double candidateElapsed_ = 0;
    bool candidateReady_ = false;
    bool ready_ = false;
};
