#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "resyne/encoding/formats/exporter.h"

namespace RSYNPresentation {

// Stateful presentation replay preserves phase, flux and spring history across
// bounded chunks. It is also the implementation used by whole-file exports.
class ReplayState {
public:
    explicit ReplayState(const RSYNPresentationSettings& settings);
    ~ReplayState();
    RSYNPresentationFrame process(const AudioColourSample& sample);
private:
    struct Impl;
    std::unique_ptr<Impl> state;
};

std::shared_ptr<RSYNPresentationData> buildPresentationData(
    const std::vector<AudioColourSample>& samples,
    const RSYNPresentationSettings& settings,
    const std::function<void(float)>& progress = {});

}
