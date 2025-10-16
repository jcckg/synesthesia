#include "resyne/ui/recorder/shared_components.h"

#include "imgui.h"

#include "resyne/recorder/colour_cache_utils.h"

namespace ReSyne::UI {

std::vector<Timeline::TimelineSample> samplePreviewData(
    RecorderState& state,
    size_t maxSamples,
    std::lock_guard<std::mutex>& lock
) {
    (void)lock;
    std::vector<Timeline::TimelineSample> previewData;

    const bool usePreview = state.importPhase == 3 && state.previewReady.load(std::memory_order_acquire);
    const auto& sourceSamples = usePreview ? state.previewSamples : state.samples;

    const size_t samplesSize = sourceSamples.size();
    if (samplesSize == 0) {
        return previewData;
    }

    if (!usePreview) {
        RecorderColourCache::ensureCacheLocked(state);
    }

    const auto convertSample = [&](size_t index) {
        Timeline::TimelineSample output{};
        output.timestamp = sourceSamples[index].timestamp;
        if (usePreview) {
            const auto entry = RecorderColourCache::computeSampleColour(
                sourceSamples[index],
                state.importGamma,
                state.importColourSpace,
                state.importGamutMapping);
            output.colour = entry.rgb;
            output.labL = entry.labL;
            output.labA = entry.labA;
            output.labB = entry.labB;
        } else {
            const auto& entry = state.sampleColourCache[index];
            output.colour = entry.rgb;
            output.labL = entry.labL;
            output.labA = entry.labA;
            output.labB = entry.labB;
        }
        return output;
    };

    if (samplesSize <= maxSamples) {
        previewData.reserve(samplesSize);
        for (size_t i = 0; i < samplesSize; ++i) {
            previewData.push_back(convertSample(i));
        }
    } else {
        previewData.reserve(maxSamples);
        const double step = static_cast<double>(samplesSize) / static_cast<double>(maxSamples);
        for (size_t i = 0; i < maxSamples; ++i) {
            const size_t index = static_cast<size_t>(i * step);
            previewData.push_back(convertSample(index));
        }
    }

    return previewData;
}

void renderStatusMessage(RecorderState& state) {
    if (state.statusMessageTimer > 0.0f && !state.statusMessage.empty()) {
        ImGui::Spacing();

        constexpr float FADE_DURATION = 1.0f;
        float alpha = 1.0f;
        if (state.statusMessageTimer < FADE_DURATION) {
            alpha = state.statusMessageTimer / FADE_DURATION;
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, alpha * 0.9f));
        ImGui::TextWrapped("%s", state.statusMessage.c_str());
        ImGui::PopStyleColor();
    }
}

}
