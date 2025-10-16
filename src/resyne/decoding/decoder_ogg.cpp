#include "resyne/decoding/decoder_ogg.h"
#include <algorithm>
#include <cmath>
#include <vector>

#define STB_VORBIS_NO_PUSHDOWN_MATH
#define STB_VORBIS_NO_PUSHDOWN_ANALYSIS
#include <stb_vorbis.c>

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

bool decodeOgg(const std::string& filepath, DecodedAudio& out, std::string& error) {
    int openError = 0;
    stb_vorbis* vorbis = stb_vorbis_open_filename(filepath.c_str(), &openError, nullptr);
    if (!vorbis) {
        error = "unable to decode ogg";
        return false;
    }

    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (info.channels <= 0 || info.sample_rate <= 0) {
        stb_vorbis_close(vorbis);
        error = "invalid ogg stream";
        return false;
    }

    const unsigned int channelCount = static_cast<unsigned int>(info.channels);
    const unsigned int sampleRate = static_cast<unsigned int>(info.sample_rate);

    std::vector<float> interleaved;
    interleaved.reserve(static_cast<SampleCount>(channelCount) * 4096);

    constexpr int CHUNK_SIZE = 4096;
    std::vector<float> chunk(static_cast<std::size_t>(CHUNK_SIZE) * channelCount);

    while (true) {
        const int framesRead = stb_vorbis_get_samples_float_interleaved(
            vorbis, static_cast<int>(channelCount), chunk.data(), static_cast<int>(chunk.size()));
        if (framesRead <= 0) {
            break;
        }
        const SampleCount samplesRead = static_cast<SampleCount>(framesRead) * channelCount;
        interleaved.insert(interleaved.end(), chunk.data(), chunk.data() + samplesRead);
    }

    stb_vorbis_close(vorbis);

    if (interleaved.empty()) {
        error = "ogg contains no audio";
        return false;
    }

    const SampleCount frameCount = interleaved.size() / channelCount;
    out.channels = channelCount;
    out.sampleRate = sampleRate;
    out.samples = convertToMono(interleaved.data(), frameCount, channelCount);

    if (out.samples.empty()) {
        error = "ogg mono conversion failed";
        return false;
    }
    return true;
}

}
