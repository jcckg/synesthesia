#pragma once

#include <memory>
#include <string>

#include "colour/colour_track.h"
#include "resyne/encoding/formats/rsyn_asset.h"

namespace ReSyne {

struct RecorderState;

class PresentationAnalysis {
public:
    void append(const RSYNPresentationFrame& frame);
    [[nodiscard]] std::size_t size() const { return size_; }
    [[nodiscard]] bool empty() const { return size_ == 0; }
    [[nodiscard]] const RSYNPresentationFrame& operator[](std::size_t index) const;
    [[nodiscard]] std::size_t indexAt(double time) const;
    [[nodiscard]] ColourPresentation::SpectralContributions contributionsAt(double time) const;
private:
    static constexpr std::size_t blockSize = 256;
    std::vector<std::shared_ptr<const std::vector<RSYNPresentationFrame>>> blocks_;
    std::size_t size_ = 0;
};

struct PresentationSnapshot {
    std::shared_ptr<const ColourPresentation::ColourTrack> colours;
    std::shared_ptr<const ColourPresentation::ColourTrack> targets;
    std::shared_ptr<const PresentationAnalysis> analysis;
    RSYNPresentationSettings settings;
    std::uint64_t sourceGeneration = 0;
    std::uint64_t sourceRevision = 0;
    std::uint64_t revision = 0;
    std::size_t processedFrames = 0;
    std::size_t sourceFrames = 0;
    double duration = 0.0;
    bool preview = false;
    std::string message;
};

class PresentationWorker {
public:
    explicit PresentationWorker(RecorderState& state);
    ~PresentationWorker();
    std::shared_ptr<const PresentationSnapshot> request();
    void retainImportPreview(std::uint64_t previewGeneration, std::uint64_t sourceGeneration);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::shared_ptr<const PresentationSnapshot> recorderPresentation(RecorderState& state);

}
