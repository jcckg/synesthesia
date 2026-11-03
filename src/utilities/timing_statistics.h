#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>

namespace Utilities {
class TimingStatistics {
public:
    struct Snapshot { double average = 0, maximum = 0, p95 = 0, p99 = 0; std::uint64_t count = 0; };
    void record(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0) return;
        const auto micros = static_cast<std::uint64_t>(std::min(seconds * 1000000, 1000000000000.0));
        const auto index = count_.load(std::memory_order_relaxed);
        recent_[index % recent_.size()].store(micros, std::memory_order_relaxed);
        auto maximum = maximum_.load(std::memory_order_relaxed);
        while (maximum < micros && !maximum_.compare_exchange_weak(maximum, micros, std::memory_order_relaxed)) {}
        count_.store(index + 1, std::memory_order_release);
    }
    Snapshot snapshot() const {
        Snapshot result;
        result.count = count_.load(std::memory_order_acquire);
        result.maximum = static_cast<double>(maximum_.load(std::memory_order_relaxed)) / 1000000;
        const auto samples = std::min<std::uint64_t>(result.count, recent_.size());
        if (samples == 0) return result;
        std::array<std::uint64_t, 41> buckets{};
        std::uint64_t total = 0;
        for (std::uint64_t i = 0; i < samples; ++i) {
            const auto micros = recent_[i].load(std::memory_order_relaxed);
            const auto bucket = std::min<std::size_t>(buckets.size() - 1, static_cast<std::size_t>(std::bit_width(micros)));
            ++buckets[bucket];
            total += micros;
        }
        result.average = static_cast<double>(total) / static_cast<double>(samples) / 1000000;
        std::uint64_t cumulative = 0;
        bool found95 = false;
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            cumulative += buckets[i];
            const double upper = std::ldexp(1.0, static_cast<int>(i)) / 1000000;
            if (!found95 && cumulative >= (samples * 95 + 99) / 100) { result.p95 = upper; found95 = true; }
            if (cumulative >= (samples * 99 + 99) / 100) { result.p99 = upper; break; }
        }
        return result;
    }
    void reset() {
        count_ = 0;
        maximum_ = 0;
        for (auto& value : recent_) value = 0;
    }
private:
    std::array<std::atomic<std::uint64_t>, 512> recent_{};
    std::atomic<std::uint64_t> count_{0}, maximum_{0};
};
}
