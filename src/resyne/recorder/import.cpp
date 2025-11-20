#include "resyne/recorder/recorder.h"
#include "resyne/recorder/import_helpers.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>

#include "resyne/encoding/formats/exporter.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "resyne/encoding/audio/wav_encoder.h"

namespace ReSyne {

namespace {

constexpr float STATUS_MESSAGE_DURATION = 4.0f;

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

    {
        std::lock_guard<std::mutex> lock(state.samplesMutex);
        state.samples = std::move(samples);
    }

    state.metadata = std::move(metadata);
    state.isRecording = false;
    state.isPlaybackInitialised = false;
    state.reconstructedAudio.clear();

    if (state.audioOutput) {
        state.audioOutput->stop();
        state.audioOutput->clearAudioData();
    }

    state.fallbackSampleRate = state.metadata.sampleRate;
    state.fallbackFftSize = state.metadata.fftSize;
    state.fallbackHopSize = state.metadata.hopSize;
    state.dropFlashAlpha = 1.0f;
    state.statusMessageTimer = STATUS_MESSAGE_DURATION;
    state.timeline.scrubberNormalisedPosition = 0.0f;
    state.timeline.isScrubberDragging = false;
    state.timeline.hoverOverlayAlpha = 0.0f;
    state.timeline.gradientRegionValid = false;
    state.timeline.zoomFactor = 1.0f;
    state.timeline.viewCenterNormalised = 0.5f;
    state.timeline.grabStartViewCenter = 0.5f;
    state.timeline.trackScrubber = false;
    state.timeline.isZoomGestureActive = false;
    state.timeline.isGrabGestureActive = false;

    RecorderColourCache::rebuildCache(state);
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
                              float gamma,
                              ColourMapper::ColourSpace colourSpace,
                              bool applyGamutMapping) {
    state.loadingProgress = 0.0f;

    std::vector<AudioColourSample> importedSamples;
    AudioMetadata metadata{};
    std::string errorMessage;
    const auto extension = extractExtension(filepath);

    bool success = false;
    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" ||
        extension == ".mpeg3" || extension == ".mpga" || extension == ".ogg" ||
        extension == ".oga") {
        success = ImportHelpers::importAudioFile(
            filepath, gamma, colourSpace, applyGamutMapping,
            state.importLowGain, state.importMidGain, state.importHighGain,
            importedSamples, metadata, errorMessage,
            [&state](float progress) { state.loadingProgress = progress; },
            nullptr
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
			filepath, gamma, colourSpace, applyGamutMapping, state.fallbackSampleRate,
			state.importLowGain, state.importMidGain, state.importHighGain,
			importedSamples, metadata, errorMessage,
			[&state](float progress) { state.loadingProgress = progress; },
			nullptr
        );
    } else {
        errorMessage = "unsupported format";
    }

    const auto filename = extractFilename(filepath);
    if (!success) {
        state.showLoadingDialog = false;
        state.dropFlashAlpha = 0.0f;
        state.statusMessage = "Failed to load " + filename;
        if (!errorMessage.empty()) {
            state.statusMessage += " (" + errorMessage + ")";
        }
        state.statusMessageTimer = STATUS_MESSAGE_DURATION;
        return false;
    }

    state.loadingProgress = 0.9f;
    applyImportedSequence(state, std::move(importedSamples), metadata);

    state.loadingProgress = 0.95f;
    reconstructAudio(state);

    state.showLoadingDialog = false;
    state.statusMessage.clear();
    state.statusMessageTimer = 0.0f;
    return true;
}

void Recorder::importFromFileThreaded(RecorderState& state,
                                      std::string filepath,
                                      float gamma,
                                      ColourMapper::ColourSpace colourSpace,
                                      bool applyGamutMapping) {
    state.importRunning.store(true, std::memory_order_release);
    state.importComplete.store(false, std::memory_order_release);
    state.loadingProgressAtomic.store(0.0f, std::memory_order_release);

    std::vector<AudioColourSample> samples;
    AudioMetadata metadata{};
    std::string errorMessage;
    const auto extension = extractExtension(filepath);
    const auto filename = extractFilename(filepath);

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
        state.loadingOperationStatus = status;
    };

    if (extension == ".wav" || extension == ".flac" || extension == ".mp3" ||
        extension == ".mpeg3" || extension == ".mpga" || extension == ".ogg" ||
        extension == ".oga") {
        setStatus("Processing audio...");
        success = ImportHelpers::importAudioFile(
            filepath, gamma, colourSpace, applyGamutMapping,
            state.importLowGain, state.importMidGain, state.importHighGain,
            samples, metadata, errorMessage,
            updateProgress,
            updatePreview
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
            filepath, gamma, colourSpace, applyGamutMapping, state.fallbackSampleRate,
            state.importLowGain, state.importMidGain, state.importHighGain,
            samples, metadata, errorMessage,
            updateProgress,
            updatePreview
        );
    } else {
        errorMessage = "unsupported format";
    }

    std::vector<float> reconstructedAudio;
    bool reconstructionSuccess = false;
    uint32_t numChannels = 1;

    const bool shouldReconstructDuringImport = (extension == ".wav" || extension == ".flac" ||
                                                 extension == ".mp3" || extension == ".mpeg3" ||
                                                 extension == ".mpga" || extension == ".ogg" ||
                                                 extension == ".oga");

    if (success && !samples.empty() && shouldReconstructDuringImport) {
        setStatus("Reconstructing audio...");
        updateProgress(0.85f);

        numChannels = !samples.empty() ? samples.front().channels : 1;

        std::vector<std::vector<float>> channelAudioData(numChannels);
        size_t maxLength = 0;
        bool allChannelsSuccess = true;

        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            std::vector<SpectralSample> spectralSamples;
            spectralSamples.reserve(samples.size());

            for (const auto& sample : samples) {
                SpectralSample spectral;
                spectral.magnitudes.clear();
                spectral.phases.clear();
                if (ch < sample.magnitudes.size()) {
                    spectral.magnitudes.push_back(sample.magnitudes[ch]);
                } else {
                    spectral.magnitudes.push_back(std::vector<float>());
                }
                if (ch < sample.phases.size()) {
                    spectral.phases.push_back(sample.phases[ch]);
                } else {
                    spectral.phases.push_back(std::vector<float>());
                }
                spectral.timestamp = sample.timestamp;
                spectral.sampleRate = sample.sampleRate;
                spectralSamples.push_back(spectral);
            }

            updateProgress(0.85f + 0.05f * (static_cast<float>(ch) / static_cast<float>(numChannels)));

            auto result = WAVEncoder::reconstructFromSpectralData(
                spectralSamples,
                metadata.sampleRate,
                metadata.fftSize,
                metadata.hopSize
            );

            if (!result.success || result.audioSamples.empty()) {
                allChannelsSuccess = false;
                break;
            }

            channelAudioData[ch] = std::move(result.audioSamples);
            maxLength = std::max(maxLength, channelAudioData[ch].size());
        }

        updateProgress(0.95f);

        if (allChannelsSuccess) {
            reconstructedAudio.clear();
            reconstructedAudio.reserve(maxLength * numChannels);

            for (size_t i = 0; i < maxLength; ++i) {
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    if (i < channelAudioData[ch].size()) {
                        reconstructedAudio.push_back(channelAudioData[ch][i]);
                    } else {
                        reconstructedAudio.push_back(0.0f);
                    }
                }
            }
            reconstructionSuccess = true;
        }
    } else if (success && !samples.empty() && !shouldReconstructDuringImport) {
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
                state.reconstructedAudio = std::move(reconstructedAudio);
            } else {
                state.reconstructedAudio.clear();
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
