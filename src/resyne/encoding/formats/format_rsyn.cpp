#include "resyne/encoding/formats/format_rsyn.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "resyne/encoding/formats/rsyn_container.h"
#include "resyne/encoding/formats/rsyn_presentation.h"
#include "resyne/encoding/formats/rsyn_serialisation.h"

namespace {

constexpr std::uint32_t kMetaTag = RSYNContainer::makeTag("META");
constexpr std::uint32_t kSourceTag = RSYNContainer::makeTag("SRCE");
constexpr std::uint32_t kSpectralTag = RSYNContainer::makeTag("SPEC");
constexpr std::uint32_t kPresentationTag = RSYNContainer::makeTag("PRES");

void emitProgress(const std::function<void(float)>& progress, const float value) {
    if (!progress) {
        return;
    }
    progress(std::clamp(value, 0.0f, 1.0f));
}

bool readRequiredLocator(const AudioMetadata& metadata,
                         const std::uint32_t tag,
                         RSYNContainer::ChunkLocator& locator) {
    if (metadata.lazyAsset == nullptr) {
        return false;
    }

    const auto it = metadata.lazyAsset->chunkIndex.find(tag);
    if (it == metadata.lazyAsset->chunkIndex.end()) {
        return false;
    }

    locator = it->second;
    return true;
}

AudioMetadata prepareMetadata(const std::vector<AudioColourSample>& samples,
                              AudioMetadata metadata,
                              const std::shared_ptr<RSYNPresentationData>& presentationData) {
    metadata.numFrames = samples.size();
    if (metadata.numBins == 0 && !samples.empty() && !samples.front().magnitudes.empty()) {
        metadata.numBins = samples.front().magnitudes.front().size();
    }
    if (metadata.channels == 0 && !samples.empty()) {
        metadata.channels = samples.front().channels;
    }
    metadata.presentationData = presentationData;
    return metadata;
}

}

namespace SequenceExporterInternal {

bool exportToRsyn(const std::string& filepath,
                  const std::vector<AudioColourSample>& samples,
                  const AudioMetadata& metadata,
                  const RSYNExportOptions& options,
                  const std::function<void(float)>& progress) {
    if (samples.empty()) {
        emitProgress(progress, 1.0f);
        return false;
    }

    emitProgress(progress, 0.02f);

    const auto presentationData = RSYNPresentation::buildPresentationData(
        samples,
        options.presentationSettings,
        [&](const float value) {
            emitProgress(progress, 0.02f + value * 0.28f);
        });
    const AudioMetadata exportedMetadata = prepareMetadata(samples, metadata, presentationData);

    std::vector<std::uint8_t> metaPayload;
    std::vector<std::uint8_t> sourcePayload;
    std::vector<std::uint8_t> spectralPayload;
    std::vector<std::uint8_t> presentationPayload;
    if (!RSYNSerialisation::encodeMetadata(exportedMetadata, metaPayload) ||
        !RSYNSerialisation::encodeSourceBytes(exportedMetadata.sourceData, sourcePayload) ||
        !RSYNSerialisation::encodeSamples(samples, spectralPayload) ||
        !RSYNSerialisation::encodePresentationFrames(exportedMetadata.presentationData, presentationPayload)) {
        emitProgress(progress, 1.0f);
        return false;
    }

    emitProgress(progress, 0.72f);

    std::vector<RSYNContainer::Chunk> chunks;
    chunks.push_back({kMetaTag, std::move(metaPayload)});
    chunks.push_back({kSpectralTag, std::move(spectralPayload)});
    chunks.push_back({kPresentationTag, std::move(presentationPayload)});
    if (!sourcePayload.empty()) {
        chunks.push_back({kSourceTag, std::move(sourcePayload)});
    }

    const bool ok = RSYNContainer::writeFile(
        filepath,
        chunks,
        [&](const float value) {
            emitProgress(progress, 0.72f + value * 0.28f);
        });
    emitProgress(progress, 1.0f);
    return ok;
}

bool loadFromRsyn(const std::string& filepath,
                  std::vector<AudioColourSample>& samples,
                  AudioMetadata& metadata,
                  const std::function<void(float)>& progress,
                  const SequenceFrameCallback& onFrameDecoded) {
    if (!loadFromRsynShell(
            filepath,
            metadata,
            [&](const float value) {
                emitProgress(progress, value * 0.5f);
            }) ||
        !hydrateRsynSamples(
            metadata,
            samples,
            [&](const float value) {
                emitProgress(progress, 0.5f + value * 0.5f);
            },
            onFrameDecoded)) {
        return false;
    }

    metadata.numFrames = samples.size();
    if (metadata.numBins == 0 && !samples.empty() && !samples.front().magnitudes.empty()) {
        metadata.numBins = samples.front().magnitudes.front().size();
    }
    if (metadata.channels == 0 && !samples.empty()) {
        metadata.channels = samples.front().channels;
    }

    emitProgress(progress, 1.0f);
    return true;
}

bool loadFromRsynShell(const std::string& filepath,
                       AudioMetadata& metadata,
                       const std::function<void(float)>& progress) {
    metadata.sourceData.reset();
    metadata.presentationData.reset();
    metadata.lazyAsset = std::make_shared<RSYNLazyAsset>();
    metadata.lazyAsset->filepath = filepath;

    if (!RSYNContainer::readIndex(
            filepath,
            metadata.lazyAsset->chunkIndex,
            [&](const float value) {
                emitProgress(progress, value * 0.3f);
            })) {
        return false;
    }

    RSYNContainer::ChunkLocator metaLocator{};
    RSYNContainer::ChunkLocator presentationLocator{};
    if (!readRequiredLocator(metadata, kMetaTag, metaLocator) ||
        !readRequiredLocator(metadata, kPresentationTag, presentationLocator)) {
        return false;
    }

    std::vector<std::uint8_t> metaPayload;
    std::vector<std::uint8_t> presentationPayload;
    if (!RSYNContainer::readChunk(metadata.lazyAsset->filepath, metaLocator, metaPayload) ||
        !RSYNContainer::readChunk(metadata.lazyAsset->filepath, presentationLocator, presentationPayload)) {
        return false;
    }

    if (!RSYNSerialisation::decodeMetadata(metaPayload, metadata)) {
        return false;
    }

    metadata.lazyAsset->filepath = filepath;
    if (!RSYNSerialisation::decodePresentationFrames(
            presentationPayload,
            metadata,
            [&](const float value) {
                emitProgress(progress, 0.3f + value * 0.7f);
            })) {
        return false;
    }

    emitProgress(progress, 1.0f);
    return true;
}

bool hydrateRsynSamples(AudioMetadata& metadata,
                        std::vector<AudioColourSample>& samples,
                        const std::function<void(float)>& progress,
                        const SequenceFrameCallback& onFrameDecoded) {
    RSYNContainer::ChunkLocator spectralLocator{};
    if (!readRequiredLocator(metadata, kSpectralTag, spectralLocator)) {
        return false;
    }

    std::vector<std::uint8_t> spectralPayload;
    if (!RSYNContainer::readChunk(metadata.lazyAsset->filepath, spectralLocator, spectralPayload) ||
        !RSYNSerialisation::decodeSamples(
            spectralPayload,
            samples,
            onFrameDecoded,
            [&](const float value) {
                emitProgress(progress, value);
            })) {
        return false;
    }

    if (metadata.numFrames == 0) {
        metadata.numFrames = samples.size();
    }
    if (metadata.numBins == 0 && !samples.empty() && !samples.front().magnitudes.empty()) {
        metadata.numBins = samples.front().magnitudes.front().size();
    }
    if (metadata.channels == 0 && !samples.empty()) {
        metadata.channels = samples.front().channels;
    }

    return true;
}

bool hydrateRsynSource(AudioMetadata& metadata,
                       const std::function<void(float)>& progress) {
    if (metadata.sourceData != nullptr && !metadata.sourceData->bytes.empty()) {
        emitProgress(progress, 1.0f);
        return true;
    }

    RSYNContainer::ChunkLocator sourceLocator{};
    if (!readRequiredLocator(metadata, kSourceTag, sourceLocator)) {
        return false;
    }

    std::vector<std::uint8_t> sourcePayload;
    if (!RSYNContainer::readChunk(metadata.lazyAsset->filepath, sourceLocator, sourcePayload)) {
        return false;
    }

    if (!RSYNSerialisation::decodeSourceBytes(sourcePayload, metadata)) {
        return false;
    }

    emitProgress(progress, 1.0f);
    return true;
}

}
