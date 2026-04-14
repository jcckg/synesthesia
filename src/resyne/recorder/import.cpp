#include "resyne/recorder/recorder.h"
#include "resyne/recorder/import_helpers.h"
#include "resyne/recorder/reconstruction_utils.h"
#include "audio/analysis/fft/fft_processor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>

#include "resyne/encoding/formats/exporter.h"
namespace ReSyne {

namespace {

constexpr float STATUS_MESSAGE_DURATION = 4.0f;
constexpr float NEUTRAL_EQ_GAIN = 1.0f;

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string extractExtension(const std::string& path) {
    std::filesystem::path fsPath(path);
    return toLower(fsPath.extension().string());
}

std::string extractFilename(const std::string& path) {
    std::filesystem::path fsPath(path);
    auto name = fsPath.filename().string();
    return name.empty() ? path : name;
}

void clampSamplesToLimit(std::vector<AudioColourSample>& samples) {
    if (samples.size() > RecorderState::MAX_SAMPLES) {
        samples.resize(RecorderState::MAX_SAMPLES);
    }
}

void applyImportedSequence(RecorderState& state,
                           std::vector<AudioColourSample>&& samples,
                           AudioMetadata metadata) {
    clampSamplesToLimit(samples);
    if (metadata.numFrames == 0) {
        metadata.numFrames = samples.size();
    }
    metadata.numBins = metadata.numBins == 0 ? static_cast<size_t>(metadata.fftSize / 2 + 1) : metadata.numBins;

    Recorder::clearLoadedAudio(state);
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.samples = std::move(samples);
    }

    state.metadata = std::move(metadata);
    state.isRecording = false;
    state.fallbackSampleRate = state.metadata.sampleRate;
    state.fallbackFftSize = state.metadata.fftSize;
    state.fallbackHopSize = state.metadata.hopSize;
    state.dropFlashAlpha = 1.0f;
    state.statusMessageTimer = STATUS_MESSAGE_DURATION;
    state.timelinePreviewCache.clear();
    state.timelinePreviewCacheDirty = true;
}

}

bool Recorder::isSupportedImportFile(const std::string& filepath) {
    const auto ext = extractExtension(filepath);
    return ext == ".wav" ||
           ext == ".flac" ||
           ext == ".mp3" ||
           ext == ".mpeg3" ||
           ext == ".mpga" ||
           ext == ".ogg" ||
           ext == ".oga" ||
           ext == ".tiff" ||
           ext == ".tif" ||
           ext == ".resyne" ||
           ext == ".synesthesia";
}

bool Recorder::importFromFile(RecorderState& state,
                              const std::string& filepath,
                              ColourCore::ColourSpace colourSpace,
                              bool applyGamutMapping) {
    state.loadingProgress = 0.0f;

    std::vector<AudioColourSample> importedSamples;
    AudioMetadata metadata{};
    std::string errorMessage;
    std::vector<float> playbackAudio;
    const auto extension = extractExtension(filepath);

    bool success = false;
    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" ||
        extension == ".mpeg3" || extension == ".mpga" || extension == ".ogg" ||
        extension == ".oga") {
        success = ImportHelpers::importAudioFile(
            filepath, colourSpace, applyGamutMapping,
            FFTProcessor::HOP_SIZE,
            NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN,
            importedSamples, metadata, errorMessage,
            [&state](float progress) { state.loadingProgress = progress; },
            nullptr,
            true,
            true,
            &playbackAudio
        );
	} else if (extension == ".tiff" || extension == ".tif") {
		state.loadingProgress = 0.05f;
		success = SequenceExporter::loadFromTIFF(
			filepath,
			importedSamples,
			metadata,
			[&state](float p) {
				state.loadingProgress = std::clamp(0.05f + p * 0.85f, 0.0f, 0.95f);
			});
		if (!success || importedSamples.empty()) {
			errorMessage = "parse failure";
		}
		state.loadingProgress = 0.9f;
	} else if (extension == ".resyne" || extension == ".synesthesia") {
		success = ImportHelpers::importResyneFile(
			filepath, colourSpace, applyGamutMapping, state.fallbackSampleRate,
			NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN,
			importedSamples, metadata, errorMessage,
			[&state](float progress) { state.loadingProgress = progress; },
			nullptr
        );
    } else {
        errorMessage = "unsupported format";
    }

    if (!success) {
        state.showLoadingDialog = false;
        state.dropFlashAlpha = 0.0f;
        state.statusMessage = "Failed to load " + extractFilename(filepath);
        if (!errorMessage.empty()) {
            state.statusMessage += " (" + errorMessage + ")";
        }
        state.statusMessageTimer = STATUS_MESSAGE_DURATION;
        return false;
    }

    state.loadingProgress = 0.9f;
    applyImportedSequence(state, std::move(importedSamples), metadata);

    state.loadingProgress = 0.95f;
    if (!playbackAudio.empty()) {
        state.playbackAudio = std::move(playbackAudio);
        refreshPlaybackOutput(state);
    } else {
        reconstructAudio(state);
    }

    state.showLoadingDialog = false;
    state.statusMessage.clear();
    state.statusMessageTimer = 0.0f;
    return true;
}

void Recorder::importFromFileThreaded(RecorderState& state,
                                      std::string filepath,
                                      ColourCore::ColourSpace colourSpace,
                                      bool applyGamutMapping) {
    state.importRunning.store(true, std::memory_order_release);
    state.importComplete.store(false, std::memory_order_release);
    state.loadingProgressAtomic.store(0.0f, std::memory_order_release);

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;
    std::vector<float> playbackAudio;
    const auto extension = extractExtension(filepath);
    bool success = false;

    auto updateProgress = [&state](float progress) {
        state.loadingProgressAtomic.store(progress, std::memory_order_release);
    };

    auto updatePreview = [&state](const std::vector<AudioColourSample>& samples) {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.previewSamples = samples;
        state.previewReady.store(true, std::memory_order_release);
    };
    auto forwardDecodedFrame = [&state](const std::vector<AudioColourSample>& decoded, size_t validCount) {
        if (validCount == 0 || decoded.empty()) {
            return;
        }
        const size_t safeCount = std::min(validCount, decoded.size());
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.previewSamples.assign(decoded.begin(), decoded.begin() + static_cast<std::ptrdiff_t>(safeCount));
        state.previewReady.store(true, std::memory_order_release);
    };

    auto setStatus = [&state](const std::string& status) {
        setLoadingOperationStatus(state, status);
    };

    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" ||
        extension == ".mpeg3" || extension == ".mpga" || extension == ".ogg" ||
        extension == ".oga") {
        setStatus("Processing audio...");
        success = ImportHelpers::importAudioFile(
            filepath, colourSpace, applyGamutMapping,
            FFTProcessor::HOP_SIZE,
            NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN,
            samples, metadata, errorMessage,
            updateProgress,
            updatePreview,
            true,
            true,
            &playbackAudio
        );
	} else if (extension == ".tiff" || extension == ".tif") {
		setStatus("Loading TIFF file...");
		success = SequenceExporter::loadFromTIFF(
			filepath,
			samples,
			metadata,
			[&updateProgress](float p) {
				updateProgress(0.05f + p * 0.75f);
			},
			forwardDecodedFrame);
		if (!success || samples.empty()) {
			errorMessage = "parse failure";
		} else {
			updatePreview(samples);
		}
	} else if (extension == ".resyne" || extension == ".synesthesia") {
		setStatus("Loading .resyne file...");
		success = ImportHelpers::importResyneFile(
            filepath, colourSpace, applyGamutMapping, state.fallbackSampleRate,
            NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN,
            samples, metadata, errorMessage,
            updateProgress,
            updatePreview
        );
    } else {
        errorMessage = "unsupported format";
    }

    std::vector<float> resolvedPlaybackAudio;
    bool reconstructionSuccess = false;

    const bool shouldReconstructDuringImport = playbackAudio.empty();

    if (success && !samples.empty() && shouldReconstructDuringImport) {
        setStatus("Reconstructing audio...");
        updateProgress(0.85f);
        reconstructionSuccess = RecorderReconstruction::buildPlaybackAudio(
            samples,
            metadata,
            resolvedPlaybackAudio,
            [&updateProgress](float fraction) {
                updateProgress(0.85f + std::clamp(fraction, 0.0f, 1.0f) * 0.10f);
            });
        updateProgress(0.95f);
    } else if (success && !samples.empty() && !shouldReconstructDuringImport) {
        resolvedPlaybackAudio = std::move(playbackAudio);
        reconstructionSuccess = !resolvedPlaybackAudio.empty();
        updateProgress(0.95f);
    }

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        if (success) {
            clampSamplesToLimit(samples);
            if (metadata.numFrames == 0) {
                metadata.numFrames = samples.size();
            }
            metadata.numBins = metadata.numBins == 0 ? static_cast<size_t>(metadata.fftSize / 2 + 1) : metadata.numBins;

            state.importedSamples = std::move(samples);
            state.importedMetadata = std::move(metadata);
            state.importErrorMessage.clear();

            if (reconstructionSuccess) {
                state.playbackAudio = std::move(resolvedPlaybackAudio);
            } else {
                state.playbackAudio.clear();
            }

            updateProgress(1.0f);
        } else {
            state.importedSamples.clear();
            state.importErrorMessage = errorMessage.empty() ? "unknown error" : errorMessage;
        }
    }

    state.importComplete.store(true, std::memory_order_release);
    state.importRunning.store(false, std::memory_order_release);
}

}
