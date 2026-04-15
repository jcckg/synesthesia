#include "resyne/recorder/embedded_source_utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "resyne/decoding/audio_decoder.h"

namespace ReSyne::EmbeddedSourceUtils {

namespace {

std::vector<float> buildInterleavedAudio(const AudioDecoding::DecodedAudio& decoded) {
    if (decoded.channelSamples.empty()) {
        return {};
    }

    const size_t channelCount = decoded.channelSamples.size();
    const size_t frameCount = decoded.channelSamples.front().size();
    std::vector<float> interleaved(frameCount * channelCount, 0.0f);

    for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex) {
        const auto& channelSamples = decoded.channelSamples[channelIndex];
        const size_t safeFrameCount = std::min(frameCount, channelSamples.size());
        for (size_t frameIndex = 0; frameIndex < safeFrameCount; ++frameIndex) {
            interleaved[frameIndex * channelCount + channelIndex] = channelSamples[frameIndex];
        }
    }

    return interleaved;
}

}

bool decodeEmbeddedSourceAudio(const AudioMetadata& metadata,
                               std::vector<float>& playbackAudio,
                               std::string& errorMessage) {
    playbackAudio.clear();

    if (metadata.sourceData == nullptr || metadata.sourceData->bytes.empty()) {
        return false;
    }

    const std::string extension = metadata.sourceData->extension.empty()
        ? ".bin"
        : metadata.sourceData->extension;
    const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path tempPath =
        std::filesystem::temp_directory_path() /
        std::filesystem::path("synesthesia_rsyn_source_" + std::to_string(uniqueId) + extension);

    {
        std::ofstream file(tempPath, std::ios::binary);
        if (!file.is_open()) {
            errorMessage = "unable to materialise embedded source";
            return false;
        }
        file.write(
            reinterpret_cast<const char*>(metadata.sourceData->bytes.data()),
            static_cast<std::streamsize>(metadata.sourceData->bytes.size()));
        if (!file.good()) {
            errorMessage = "unable to materialise embedded source";
            return false;
        }
    }

    AudioDecoding::DecodedAudio decoded;
    const bool decodedOk = AudioDecoding::decodeFile(tempPath.string(), decoded, errorMessage);
    std::error_code removeError;
    std::filesystem::remove(tempPath, removeError);
    if (!decodedOk) {
        return false;
    }

    playbackAudio = buildInterleavedAudio(decoded);
    return !playbackAudio.empty();
}

}
