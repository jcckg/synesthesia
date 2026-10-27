#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "colour/colour_core.h"

namespace ColourPresentation {

using ColourTriple = std::array<double, 3>;

inline ColourTriple toColourTriple(const std::array<float, 3>& xyz) {
    return {static_cast<double>(xyz[0]), static_cast<double>(xyz[1]), static_cast<double>(xyz[2])};
}

struct TrackPoint {
    double time = 0.0;
    ColourTriple colour{};
    float responseSeconds = 0.0f;
};

class ColourTrack {
public:
    void append(const TrackPoint& point);
    [[nodiscard]] std::shared_ptr<const ColourTrack> snapshot() const;
    [[nodiscard]] std::size_t size() const;
    void prepareProjection(ColourCore::ColourSpace space, bool gamut = true) const;
    [[nodiscard]] double endTime() const;
    [[nodiscard]] TrackPoint pointAt(double time) const;
    [[nodiscard]] ColourTriple xyzAt(double time) const;
    [[nodiscard]] ColourTriple averageXYZ(double start, double end) const;
    [[nodiscard]] ColourCore::RGB rgbAt(double time, ColourCore::ColourSpace space, bool gamut = true) const;
    [[nodiscard]] ColourCore::RGB averageRGB(double start, double end,
                                            ColourCore::ColourSpace space, bool gamut = true) const;

private:
    struct Block {
        Block() = default;
        Block(const Block& other) : points(other.points), integral(other.integral), precedingIntegral(other.precedingIntegral) {}
        mutable std::mutex projectionMutex;
        mutable std::array<std::shared_ptr<const std::vector<ColourTriple>>,6> projectedIntegrals{};
        std::vector<TrackPoint> points;
        std::vector<ColourTriple> integral;
        ColourTriple precedingIntegral{};
    };
    static constexpr std::size_t blockSize = 256;
    std::vector<std::shared_ptr<const Block>> blocks_;
    std::size_t size_ = 0;
    [[nodiscard]] std::size_t blockAt(double time) const;
    [[nodiscard]] std::shared_ptr<const std::vector<ColourTriple>> projectionData(
        const Block& block, ColourCore::ColourSpace space, bool gamut) const;
    [[nodiscard]] ColourTriple projectedIntegral(const Block& block, double time,
        ColourCore::ColourSpace space, bool gamut) const;
    [[nodiscard]] ColourTriple localIntegral(const Block& block, double time) const;
    [[nodiscard]] ColourTriple segmentIntegral(const TrackPoint& a, const TrackPoint& b,
                                               double start, double end) const;
};

ColourCore::RGB projectXYZ(const ColourTriple& xyz, ColourCore::ColourSpace space, bool gamut = true);

}
