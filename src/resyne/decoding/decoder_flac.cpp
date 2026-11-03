#include "utilities/cancellation.h"
#include <memory>
#include "resyne/decoding/decoder_flac.h"
#include <algorithm>
#include <cmath>
#include <limits>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

namespace AudioDecoding {
bool decodeFlac(const std::string& filepath, DecodedAudio& out, std::string& error) {
    std::unique_ptr<drflac, decltype(&drflac_close)> decoder(drflac_open_file(filepath.c_str(), nullptr), drflac_close);
    if (!decoder) { error = "unable to decode flac"; return false; }
    const auto channels = decoder->channels;
    out.channels = channels;
    out.sampleRate = decoder->sampleRate;
    if (channels == 0 || out.sampleRate == 0) { error = "invalid flac stream"; return false; }
    out.channelSamples.assign(channels, {});
    std::vector<float> chunk(static_cast<std::size_t>(channels) * 4096);
    for (;;) {
        Utilities::checkCancellation();
        const auto count = drflac_read_pcm_frames_f32(decoder.get(), 4096, chunk.data());
        if (count == 0) break;
        for (std::size_t frame = 0; frame < count; ++frame)
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto value = chunk[frame * channels + channel];
                out.channelSamples[channel].push_back(std::isfinite(value) ? value : 0);
            }
    }
    if (out.channelSamples.front().empty()) { error = "Audio contains no samples"; return false; }
    return true;
}

bool probeFlacDurationSeconds(const std::string& filepath, double& durationSeconds, std::string& error) {
    durationSeconds = 0.0;

    drflac* flac = drflac_open_file(filepath.c_str(), nullptr);
    if (flac == nullptr) {
        error = "unable to open flac";
        return false;
    }

    const auto frameCount = flac->totalPCMFrameCount;
    const auto sampleRate = flac->sampleRate;
    drflac_close(flac);

    if (frameCount == 0 || sampleRate == 0) {
        error = "invalid flac stream";
        return false;
    }

    durationSeconds = static_cast<double>(frameCount) / static_cast<double>(sampleRate);
    return durationSeconds > 0.0;
}

}
