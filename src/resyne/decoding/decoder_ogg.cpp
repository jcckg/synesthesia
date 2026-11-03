#include "utilities/cancellation.h"
#include <memory>
#include "resyne/decoding/decoder_ogg.h"
#include <algorithm>
#include <cmath>
#include <vector>

#define STB_VORBIS_NO_PUSHDOWN_MATH
#define STB_VORBIS_NO_PUSHDOWN_ANALYSIS
#include <stb_vorbis.c>

namespace AudioDecoding {
bool decodeOgg(const std::string& filepath, DecodedAudio& out, std::string& error) {
    int openError = 0;
    stb_vorbis* vorbis = stb_vorbis_open_filename(filepath.c_str(), &openError, nullptr);
    if (!vorbis) {
        error = "unable to decode ogg";
        return false;
    }

    std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> handle(vorbis, stb_vorbis_close);
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (info.channels <= 0 || info.sample_rate <= 0) {
        error = "invalid ogg stream";
        return false;
    }

    const unsigned int channelCount = static_cast<unsigned int>(info.channels);
    const unsigned int sampleRate = static_cast<unsigned int>(info.sample_rate);

    out.channelSamples.assign(channelCount, {});

    constexpr int CHUNK_SIZE = 4096;
    std::vector<float> chunk(static_cast<std::size_t>(CHUNK_SIZE) * channelCount);

    while (true) {
        Utilities::checkCancellation();
        const int framesRead = stb_vorbis_get_samples_float_interleaved(
            vorbis, static_cast<int>(channelCount), chunk.data(), static_cast<int>(chunk.size()));
        if (framesRead <= 0) {
            break;
        }
        for (int frame = 0; frame < framesRead; ++frame)
            for (std::size_t channel = 0; channel < channelCount; ++channel) {
                const float value = chunk[static_cast<std::size_t>(frame) * channelCount + channel];
                out.channelSamples[channel].push_back(std::isfinite(value) ? value : 0);
            }
    }

    out.channels = channelCount;
    out.sampleRate = sampleRate;
    if (out.channelSamples.front().empty()) { error = "ogg contains no audio"; return false; }

    return true;
}

bool probeOggDurationSeconds(const std::string& filepath, double& durationSeconds, std::string& error) {
    durationSeconds = 0.0;

    int openError = 0;
    stb_vorbis* vorbis = stb_vorbis_open_filename(filepath.c_str(), &openError, nullptr);
    if (!vorbis) {
        error = "unable to open ogg";
        return false;
    }

    const float seconds = stb_vorbis_stream_length_in_seconds(vorbis);
    stb_vorbis_close(vorbis);

    if (!std::isfinite(seconds) || seconds <= 0.0f) {
        error = "invalid ogg stream";
        return false;
    }

    durationSeconds = static_cast<double>(seconds);
    return true;
}

}
