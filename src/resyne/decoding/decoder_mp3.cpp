#include "resyne/decoding/decoder_mp3.h"
#include <algorithm>
#include <cmath>
#include <limits>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

namespace AudioDecoding {
namespace {

using SampleCount = std::size_t;

std::vector<std::vector<float>> deinterleave(const float* interleaved,
                                              SampleCount frameCount,
                                              std::uint32_t channels) {
    std::vector<std::vector<float>> channelSamples;
    if (!interleaved || frameCount == 0 || channels == 0) {
        return channelSamples;
    }

    channelSamples.resize(channels);
    for (std::uint32_t ch = 0; ch < channels; ++ch) {
        channelSamples[ch].reserve(frameCount);
    }

    for (SampleCount frame = 0; frame < frameCount; ++frame) {
        const SampleCount baseIndex = frame * static_cast<SampleCount>(channels);
        for (std::uint32_t ch = 0; ch < channels; ++ch) {
            const float raw = interleaved[baseIndex + ch];
            channelSamples[ch].push_back(std::isfinite(raw) ? raw : 0.0f);
        }
    }
    return channelSamples;
}

}

bool decodeMp3(const std::string& filepath, DecodedAudio& out, std::string& error) {
    drmp3_config config{};
    drmp3_uint64 frameCount = 0;
    float* data = drmp3_open_file_and_read_pcm_frames_f32(
        filepath.c_str(), &config, &frameCount, nullptr);
    if (!data) {
        error = "unable to decode mp3";
        return false;
    }

    if (frameCount == 0 || config.channels == 0 || config.sampleRate == 0) {
        drmp3_free(data, nullptr);
        error = "empty mp3";
        return false;
    }

    const std::uint64_t totalSamples64 = frameCount * static_cast<drmp3_uint64>(config.channels);
    if (totalSamples64 > static_cast<std::uint64_t>(std::numeric_limits<SampleCount>::max())) {
        drmp3_free(data, nullptr);
        error = "mp3 too large";
        return false;
    }

    const SampleCount totalSamples = static_cast<SampleCount>(totalSamples64);
    std::vector<float> interleaved(totalSamples);
    std::copy(data, data + totalSamples, interleaved.begin());
    drmp3_free(data, nullptr);

    out.channels = config.channels;
    out.sampleRate = config.sampleRate;
    out.channelSamples = deinterleave(interleaved.data(), frameCount, config.channels);
    if (out.channelSamples.empty()) {
        error = "mp3 deinterleave failed";
        return false;
    }
    return true;
}

}
