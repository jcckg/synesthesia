#include "utilities/cancellation.h"
#include <memory>
#include "resyne/decoding/decoder_mp3.h"
#include <algorithm>
#include <cmath>
#include <limits>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>

namespace AudioDecoding {
bool decodeMp3(const std::string& filepath, DecodedAudio& out, std::string& error) {
    drmp3 decoder{};
    if (!drmp3_init_file(&decoder, filepath.c_str(), nullptr)) { error = "unable to decode mp3"; return false; }
    const auto release = [](drmp3* value) { drmp3_uninit(value); };
    std::unique_ptr<drmp3, decltype(release)> handle(&decoder, release);
    const auto channels = decoder.channels;
    out.channels = channels;
    out.sampleRate = decoder.sampleRate;
    if (channels == 0 || out.sampleRate == 0) { error = "invalid mp3 stream"; return false; }
    out.channelSamples.assign(channels, {});
    std::vector<float> chunk(static_cast<std::size_t>(channels) * 4096);
    for (;;) {
        Utilities::checkCancellation();
        const auto count = drmp3_read_pcm_frames_f32(&decoder, 4096, chunk.data());
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

bool probeMp3DurationSeconds(const std::string& filepath, double& durationSeconds, std::string& error) {
    durationSeconds = 0.0;

    drmp3 mp3{};
    if (!drmp3_init_file(&mp3, filepath.c_str(), nullptr)) {
        error = "unable to open mp3";
        return false;
    }

    const auto frameCount = drmp3_get_pcm_frame_count(&mp3);
    const auto sampleRate = mp3.sampleRate;
    drmp3_uninit(&mp3);

    if (frameCount == 0 || sampleRate == 0) {
        error = "invalid mp3 stream";
        return false;
    }

    durationSeconds = static_cast<double>(frameCount) / static_cast<double>(sampleRate);
    return durationSeconds > 0.0;
}

}
