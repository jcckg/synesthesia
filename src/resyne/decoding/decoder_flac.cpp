#include "resyne/decoding/decoder_flac.h"
#include <algorithm>
#include <cmath>
#include <limits>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

namespace AudioDecoding {
namespace {

using SampleCount = std::size_t;

std::vector<float> convertToMono(const float* interleaved,
                                 SampleCount frameCount,
                                 std::uint32_t channels) {
    std::vector<float> mono;
    if (!interleaved || frameCount == 0 || channels == 0) {
        return mono;
    }

    mono.resize(frameCount);
    const double channelScale = 1.0 / static_cast<double>(channels);
    for (SampleCount frame = 0; frame < frameCount; ++frame) {
        double accum = 0.0;
        const SampleCount baseIndex = frame * static_cast<SampleCount>(channels);
        for (std::uint32_t ch = 0; ch < channels; ++ch) {
            const float raw = interleaved[baseIndex + ch];
            accum += std::isfinite(raw) ? static_cast<double>(raw) : 0.0;
        }
        mono[frame] = static_cast<float>(accum * channelScale);
    }
    return mono;
}

}

bool decodeFlac(const std::string& filepath, DecodedAudio& out, std::string& error) {
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drflac_uint64 pcmFrameCount = 0;
    float* data = drflac_open_file_and_read_pcm_frames_f32(
        filepath.c_str(), &channels, &sampleRate, &pcmFrameCount, nullptr);
    if (!data) {
        error = "unable to decode flac";
        return false;
    }

    const std::uint64_t totalSamples64 = pcmFrameCount * static_cast<drflac_uint64>(channels);
    if (totalSamples64 == 0 || channels == 0 || sampleRate == 0) {
        drflac_free(data, nullptr);
        error = "empty flac";
        return false;
    }

    if (totalSamples64 > static_cast<std::uint64_t>(std::numeric_limits<SampleCount>::max())) {
        drflac_free(data, nullptr);
        error = "flac too large";
        return false;
    }

    const SampleCount totalSamples = static_cast<SampleCount>(totalSamples64);
    std::vector<float> interleaved(totalSamples);
    std::copy(data, data + totalSamples, interleaved.begin());
    drflac_free(data, nullptr);

    out.channels = channels;
    out.sampleRate = sampleRate;
    out.samples = convertToMono(interleaved.data(), totalSamples / channels, channels);
    if (out.samples.empty()) {
        error = "flac mono conversion failed";
        return false;
    }
    return true;
}

}
