#include "colour/colour_track.h"

#include <algorithm>
#include <cmath>

namespace ColourPresentation {
namespace {

ColourTriple add(ColourTriple a, const ColourTriple& b, double factor = 1.0) {
    for (std::size_t i = 0; i < 3; ++i) a[i] += factor * b[i];
    return a;
}

TrackPoint interpolate(const TrackPoint& a, const TrackPoint& b, double time) {
    const double fraction = b.time > a.time ? std::clamp((time - a.time) / (b.time - a.time), 0.0, 1.0) : 1.0;
    TrackPoint point{time};
    for (std::size_t i = 0; i < 3; ++i) point.colour[i] = std::lerp(a.colour[i], b.colour[i], fraction);
    point.responseSeconds = std::lerp(a.responseSeconds, b.responseSeconds, static_cast<float>(fraction));
    return point;
}

ColourTriple displayedLinear(const ColourTriple& xyz, ColourCore::ColourSpace space, bool gamut) {
    ColourCore::RGB linear;
    ColourCore::XYZtoRGB(static_cast<float>(xyz[0]),static_cast<float>(xyz[1]),static_cast<float>(xyz[2]),
        linear.r,linear.g,linear.b,space,false,gamut);
    return {std::clamp(static_cast<double>(linear.r),0.0,1.0),std::clamp(static_cast<double>(linear.g),0.0,1.0),
        std::clamp(static_cast<double>(linear.b),0.0,1.0)};
}

// https://www.pbr-book.org/4ed/Sampling_and_Reconstruction/Sampling_Theory
ColourTriple displayedSegmentIntegral(const TrackPoint& first, const TrackPoint& last,
    double start, double end, ColourCore::ColourSpace space, bool gamut) {
    if (end <= start) return {};
    const auto linearAt = [&](double time) { return displayedLinear(interpolate(first,last,time).colour,space,gamut); };
    const auto quadrature = [&](double a, double b, const ColourTriple& left,
                                const ColourTriple& middle, const ColourTriple& right) {
        return add({}, add(add(left, middle, 4.0), right), (b - a) / 6.0);
    };
    const auto integrate = [&](auto&& self, double a, double b, const ColourTriple& left,
                               const ColourTriple& middle, const ColourTriple& right,
                               const ColourTriple& whole, int depth) -> ColourTriple {
        const double centre = (a + b) * .5;
        const auto quarter = linearAt((a + centre) * .5), thirdQuarter = linearAt((centre + b) * .5);
        const auto first = quadrature(a, centre, left, quarter, middle);
        const auto last = quadrature(centre, b, middle, thirdQuarter, right);
        const auto split = add(first, last);
        double error = 0.0;
        for (std::size_t c = 0; c < 3; ++c) error = std::max(error, std::abs(split[c] - whole[c]));
        if (depth == 0 || error <= 1e-7 * (b - a)) return split;
        return add(self(self, a, centre, left, quarter, middle, first, depth - 1),
            self(self, centre, b, middle, thirdQuarter, right, last, depth - 1));
    };
    const auto left = linearAt(start), middle = linearAt((start + end) * .5), right = linearAt(end);
    return integrate(integrate,start,end,left,middle,right,quadrature(start,end,left,middle,right),12);
}

}

ColourTriple ColourTrack::segmentIntegral(const TrackPoint& a, const TrackPoint& b,
                                          double start, double end) const {
    if (end <= start) return {};
    const auto first = interpolate(a, b, start), last = interpolate(a, b, end);
    return add({}, add(first.colour, last.colour), (end - start) * 0.5);
}

void ColourTrack::append(const TrackPoint& point) {
    if (!std::isfinite(point.time) || !std::all_of(point.colour.begin(), point.colour.end(),
        [](double value) { return std::isfinite(value); })) return;
    if (!blocks_.empty() && point.time < endTime()) return;
    if (blocks_.empty() || (blocks_.back()->points.size() >= blockSize && point.time > endTime())) {
        auto next = std::make_shared<Block>();
        if (!blocks_.empty()) {
            next->points.push_back(blocks_.back()->points.back());
            next->integral.push_back({});
            next->precedingIntegral = add(blocks_.back()->precedingIntegral, blocks_.back()->integral.back());
        }
        blocks_.push_back(std::move(next));
    }
    if (blocks_.back().use_count() > 1) blocks_.back() = std::make_shared<Block>(*blocks_.back());
    auto& block = *std::const_pointer_cast<Block>(blocks_.back());
    {
        std::lock_guard lock(block.projectionMutex);
        block.projectedIntegrals.fill(nullptr);
    }
    if (!block.points.empty() && point.time == block.points.back().time) {
        block.points.back() = point;
        if (block.points.size() > 1) {
            const auto index = block.points.size() - 1;
            block.integral.back() = add(block.integral[index - 1], segmentIntegral(
                block.points[index - 1], point, block.points[index - 1].time, point.time));
        }
        return;
    }
    const auto integral = block.points.empty() ? ColourTriple{} : add(block.integral.back(),
        segmentIntegral(block.points.back(), point, block.points.back().time, point.time));
    block.points.push_back(point);
    block.integral.push_back(integral);
    ++size_;
}

std::shared_ptr<const ColourTrack> ColourTrack::snapshot() const { return std::make_shared<ColourTrack>(*this); }
std::size_t ColourTrack::size() const { return size_; }
double ColourTrack::endTime() const { return blocks_.empty() ? 0.0 : blocks_.back()->points.back().time; }

std::size_t ColourTrack::blockAt(double time) const {
    const auto found = std::lower_bound(blocks_.begin(), blocks_.end(), time,
        [](const auto& block, double value) { return block->points.back().time < value; });
    return found == blocks_.end() ? blocks_.size() - 1 : static_cast<std::size_t>(found - blocks_.begin());
}

TrackPoint ColourTrack::pointAt(double time) const {
    if (blocks_.empty()) return {};
    if (!std::isfinite(time)) time = 0.0;
    const auto& points = blocks_[blockAt(time)]->points;
    const auto next = std::upper_bound(points.begin(), points.end(), time,
        [](double value, const auto& point) { return value < point.time; });
    if (next == points.begin()) return points.front();
    if (next == points.end()) return points.back();
    return interpolate(*(next - 1), *next, time);
}

ColourTriple ColourTrack::xyzAt(double time) const { return pointAt(time).colour; }

ColourTriple ColourTrack::localIntegral(const Block& block, double time) const {
    const auto& points = block.points;
    if (time <= points.front().time) return {};
    if (time >= points.back().time) return block.integral.back();
    const auto next = std::upper_bound(points.begin(), points.end(), time,
        [](double value, const auto& point) { return value < point.time; });
    const auto index = static_cast<std::size_t>(next - points.begin() - 1);
    return add(block.integral[index], segmentIntegral(points[index], *next, points[index].time, time));
}

ColourTriple ColourTrack::averageXYZ(double start, double end) const {
    if (blocks_.empty()) return {};
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start) return xyzAt(start);
    const double firstTime = blocks_.front()->points.front().time, lastTime = endTime();
    ColourTriple integral{};
    if (start < firstTime) integral = add(integral, xyzAt(firstTime), std::min(end, firstTime) - start);
    if (end > lastTime) integral = add(integral, xyzAt(lastTime), end - std::max(start, lastTime));
    const double a = std::max(start, firstTime), b = std::min(end, lastTime);
    if (b > a) {
        const auto left = blockAt(a), right = blockAt(b);
        if (left == right) {
            integral = add(add(integral, localIntegral(*blocks_[right], b)), localIntegral(*blocks_[left], a), -1.0);
        } else {
            integral = add(integral, add(blocks_[left]->integral.back(), localIntegral(*blocks_[left], a), -1.0));
            integral = add(integral, localIntegral(*blocks_[right], b));
            if (right > left + 1) {
                integral = add(integral, add(blocks_[right]->precedingIntegral,
                    add(blocks_[left]->precedingIntegral, blocks_[left]->integral.back()), -1.0));
            }
        }
    }
    for (double& value : integral) value /= end - start;
    return integral;
}

ColourCore::RGB projectXYZ(const ColourTriple& xyz, ColourCore::ColourSpace space, bool gamut) {
    ColourCore::RGB rgb{};
    ColourCore::XYZtoRGB(static_cast<float>(xyz[0]), static_cast<float>(xyz[1]), static_cast<float>(xyz[2]), rgb.r, rgb.g, rgb.b, space, true, gamut);
    return rgb;
}

ColourCore::RGB ColourTrack::rgbAt(double time, ColourCore::ColourSpace space, bool gamut) const {
    return projectXYZ(xyzAt(time), space, gamut);
}

std::shared_ptr<const std::vector<ColourTriple>> ColourTrack::projectionData(
    const Block& block, ColourCore::ColourSpace space, bool gamut) const {
    const std::size_t profile = space == ColourCore::ColourSpace::Rec2020 ? 0 :
        space == ColourCore::ColourSpace::DisplayP3 ? 1 : 2;
    const std::size_t index = profile * 2 + static_cast<std::size_t>(gamut);
    {
        std::lock_guard lock(block.projectionMutex);
        const auto cached = block.projectedIntegrals[index];
        if (cached) return cached;
    }
    auto integral = std::make_shared<std::vector<ColourTriple>>(block.points.size());
    for (std::size_t i = 1; i < block.points.size(); ++i)
        (*integral)[i] = add((*integral)[i-1], displayedSegmentIntegral(block.points[i-1], block.points[i],
            block.points[i-1].time, block.points[i].time, space, gamut));
    std::lock_guard lock(block.projectionMutex);
    auto& cached = block.projectedIntegrals[index];
    if (!cached) cached = std::move(integral);
    return cached;
}

void ColourTrack::prepareProjection(ColourCore::ColourSpace space, bool gamut) const {
    for (const auto& block : blocks_) (void)projectionData(*block,space,gamut);
}

ColourTriple ColourTrack::projectedIntegral(const Block& block, double time,
    ColourCore::ColourSpace space, bool gamut) const {
    const auto& points = block.points;
    if (time <= points.front().time) return {};
    const auto integral = projectionData(block,space,gamut);
    if (time >= points.back().time) return integral->back();
    const auto next = std::upper_bound(points.begin(),points.end(),time,
        [](double value,const TrackPoint& point) { return value < point.time; });
    const auto index = static_cast<std::size_t>(next - points.begin() - 1);
    return add((*integral)[index],displayedSegmentIntegral(points[index],*next,points[index].time,time,space,gamut));
}

ColourCore::RGB ColourTrack::averageRGB(double start, double end, ColourCore::ColourSpace space, bool gamut) const {
    if (blocks_.empty()) return {};
    if (!std::isfinite(start) || !std::isfinite(end) || end <= start) return rgbAt(start,space,gamut);
    const double firstTime = blocks_.front()->points.front().time, lastTime = endTime();
    ColourTriple integral{};
    if (start < firstTime) integral = add(integral,displayedLinear(xyzAt(firstTime),space,gamut),std::min(end,firstTime)-start);
    if (end > lastTime) integral = add(integral,displayedLinear(xyzAt(lastTime),space,gamut),end-std::max(start,lastTime));
    const double a = std::max(start,firstTime), b = std::min(end,lastTime);
    if (b > a) {
        const auto left = blockAt(a), right = blockAt(b);
        if (left == right) integral = add(integral,add(projectedIntegral(*blocks_[left],b,space,gamut),
            projectedIntegral(*blocks_[left],a,space,gamut),-1.0));
        else {
            integral = add(integral,add(projectionData(*blocks_[left],space,gamut)->back(),
                projectedIntegral(*blocks_[left],a,space,gamut),-1.0));
            integral = add(integral,projectedIntegral(*blocks_[right],b,space,gamut));
            for (std::size_t i = left + 1; i < right; ++i) integral = add(integral,projectionData(*blocks_[i],space,gamut)->back());
        }
    }
    for (auto& component : integral) component /= end - start;
    return ColourCore::encodeRGB({static_cast<float>(integral[0]),static_cast<float>(integral[1]),
        static_cast<float>(integral[2])},space);
}

}
