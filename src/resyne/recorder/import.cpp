#include "resyne/recorder/recorder.h"
#include "utilities/cancellation.h"
#include "resyne/recorder/import_helpers.h"
#include "resyne/recorder/reconstruction_utils.h"
#include "audio/analysis/fft/fft_processor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
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

void applyImportedSequence(RecorderState& state,
                           std::vector<AudioColourSample>&& samples,
                           AudioMetadata metadata) {
    if (metadata.numFrames == 0 && !samples.empty()) {
        metadata.numFrames = samples.size();
    }
    if (metadata.numBins == 0 && metadata.fftSize > 0) {
        metadata.numBins = static_cast<size_t>(metadata.fftSize / 2 + 1);
    }

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
}

}

void Recorder::requestImport(RecorderState& state, const std::string& filepath) {
    if (!isSupportedImportFile(filepath) || state.isRecording || state.exportRunning.load(std::memory_order_acquire)) return;
    state.pendingImportPath = filepath;
    if (state.importPhase >= 3 || state.importRunning.load(std::memory_order_acquire)) {
        state.importCancellation.request_stop();
        state.importPhase = 4;
    } else {
        state.importPhase = 1;
    }
}

void Recorder::cancelImport(RecorderState& state) {
    if (state.importPhase == 0 && !state.importRunning.load(std::memory_order_acquire)) return;
    state.importCancellation.request_stop();
    state.pendingImportPath.clear();
    state.importPhase = 4;
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
           ext == ".rsyn";
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
            filepath,
            0,
            importedSamples, metadata, errorMessage,
            [&state](float progress) { state.loadingProgress = progress; },
            nullptr,
            &playbackAudio,
            RecorderState::MAX_RECORDING_SAMPLES
        );
	} else if (extension == ".tiff" || extension == ".tif") {
		state.loadingProgress = 0.05f;
		success = SequenceExporter::loadFromTIFF(
			filepath,
			importedSamples,
			metadata,
			[&state](float p) {
				state.loadingProgress = std::clamp(0.05f + p * 0.85f, 0.0f, 0.95f);
			},
            {},
            &errorMessage);
		if (!success || importedSamples.empty()) {
            success = false;
            if (errorMessage.empty()) {
                errorMessage = "TIFF contains no decodable audio frames";
            }
		}
		state.loadingProgress = 0.9f;
	} else if (extension == ".rsyn") {
		success = ImportHelpers::importRsynFile(
			filepath, colourSpace, applyGamutMapping, state.fallbackSampleRate,
			NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN,
			importedSamples, metadata, errorMessage,
			[&state](float progress) { state.loadingProgress = progress; },
			nullptr,
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
    } else if (!state.samples.empty()) {
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
    const auto cancellation = state.importCancellation.get_token();
    Utilities::checkCancellation(cancellation);
    state.importRunning.store(true, std::memory_order_release);
    state.importComplete.store(false, std::memory_order_release);
    state.loadingProgressAtomic.store(0.0f, std::memory_order_release);

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;
    std::vector<float> playbackAudio;
    const auto extension = extractExtension(filepath);
    bool success = false;

    auto updateProgress = [&state, cancellation](float progress) {
        Utilities::checkCancellation(cancellation);
        const float current = state.loadingProgressAtomic.load(std::memory_order_relaxed);
        state.loadingProgressAtomic.store(std::max(current, std::clamp(progress, 0.0f, 0.95f)),
            std::memory_order_release);
    };

    auto forwardDecodedFrame = [&state, &metadata, cancellation](const std::vector<AudioColourSample>& decoded, size_t validCount) {
        Utilities::checkCancellation(cancellation);
        const size_t safeCount = std::min(validCount, decoded.size());
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.previewMetadata = metadata;
        const size_t previousCount = state.previewSamples.size();
        if (safeCount > previousCount) {
            state.previewSamples.insert(state.previewSamples.end(), decoded.begin() + static_cast<std::ptrdiff_t>(previousCount),
                decoded.begin() + static_cast<std::ptrdiff_t>(safeCount));
        }
        state.previewReady.store(true, std::memory_order_release);
    };
    auto updatePreview = [&forwardDecodedFrame](const std::vector<AudioColourSample>& decoded) {
        forwardDecodedFrame(decoded, decoded.size());
    };

    auto setStatus = [&state, cancellation](const std::string& status) {
        Utilities::checkCancellation(cancellation);
        setLoadingOperationStatus(state, status);
    };

    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" ||
        extension == ".mpeg3" || extension == ".mpga" || extension == ".ogg" ||
        extension == ".oga") {
        setStatus("Processing audio...");
        success = ImportHelpers::importAudioFile(
            filepath,
            0,
            samples, metadata, errorMessage,
            updateProgress,
            updatePreview,
            &playbackAudio,
            RecorderState::MAX_RECORDING_SAMPLES
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
			forwardDecodedFrame,
            &errorMessage);
		if (!success || (samples.empty() && metadata.presentationData == nullptr)) {
            success = false;
            if (errorMessage.empty()) {
                errorMessage = "TIFF contains no decodable audio frames";
            }
		} else {
			updatePreview(samples);
		}
	} else if (extension == ".rsyn") {
		setStatus("Loading .rsyn file...");
		success = ImportHelpers::importRsynFile(
            filepath, colourSpace, applyGamutMapping, state.fallbackSampleRate,
            NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN, NEUTRAL_EQ_GAIN,
            samples, metadata, errorMessage,
            updateProgress,
            updatePreview,
            &playbackAudio
        );
    } else {
        errorMessage = "unsupported format";
    }

    Utilities::checkCancellation(cancellation);
    if (success) {
        std::lock_guard lock(state.samplesMutex);
        bool changed = false;
        for (size_t index = 0; index < std::min(samples.size(), state.previewSamples.size()); ++index) {
            const auto& current = samples[index];
            const auto& preview = state.previewSamples[index];
            if ((current.loudnessLUFS != preview.loudnessLUFS &&
                !(std::isnan(current.loudnessLUFS) && std::isnan(preview.loudnessLUFS))) ||
                current.bass != preview.bass) {
                changed = true;
                break;
            }
        }
        if (changed) {
            ++state.presentationPreviewRevision;
            state.previewSamples = samples;
        } else if (samples.size() > state.previewSamples.size()) {
            state.previewSamples.insert(state.previewSamples.end(),
                samples.begin() + static_cast<std::ptrdiff_t>(state.previewSamples.size()), samples.end());
        }
        state.previewMetadata = metadata;
        state.previewReady.store(true, std::memory_order_release);
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
    } else if (success) {
        updateProgress(0.95f);
    }

    Utilities::checkCancellation(cancellation);
    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        if (success) {
            if (metadata.numFrames == 0 && !samples.empty()) {
                metadata.numFrames = samples.size();
            }
            if (metadata.numBins == 0 && metadata.fftSize > 0) {
                metadata.numBins = static_cast<size_t>(metadata.fftSize / 2 + 1);
            }

            state.importedSamples = std::move(samples);
            state.importedMetadata = std::move(metadata);
            state.importErrorMessage.clear();

            if (reconstructionSuccess) {
                state.importedPlaybackAudio = std::move(resolvedPlaybackAudio);
            } else {
                state.importedPlaybackAudio.clear();
            }

            updateProgress(0.95f);
        } else {
            state.importedSamples.clear();
            state.importErrorMessage = errorMessage.empty() ? "unknown error" : errorMessage;
        }
    }

    state.importComplete.store(true, std::memory_order_release);
    state.importRunning.store(false, std::memory_order_release);
}

}
