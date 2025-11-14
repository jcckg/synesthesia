#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "audio_input.h"
#include "controls.h"
#include "colour_mapper.h"
#include "equaliser.h"
#include "fft_processor.h"
#include "smoothing.h"
#include "styling.h"
#include "spectrum_analyser.h"
#include "device_manager.h"
#include "resyne/controller/controller.h"
#include "resyne/recorder/colour_cache_utils.h"
#include "resyne/ui/timeline/timeline.h"
#include "resyne/ui/timeline/timeline_gradient.h"
#include "sidebar.h"
#include "ui/dragdrop/file_drop_manager.h"
#include "ui/input/trackpad_gestures.h"
#include "system_theme_detector.h"
#include "ui/handlers/import_handler.h"
#ifdef ENABLE_MIDI
#include "ui/handlers/midi_handler.h"
#endif
#ifdef ENABLE_API_SERVER
#include "synesthesia_api_integration.h"
#endif

void initialiseApp(UIState& state) {
    if (!state.updateState.hasCheckedThisSession) {
        state.updateChecker.checkForUpdates("jcckg", "synesthesia");
        state.updateState.hasCheckedThisSession = true;
    }
    
#ifdef ENABLE_API_SERVER
    auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
    if (api.isServerRunning() && !state.apiServerEnabled) {
        api.stopServer();
    }
#endif
}

namespace {

struct ColourUpdateContext {
	float deltaTime;
	bool smoothingEnabled;
	SpringSmoother& colourSmoother;
	float* clearColour;
	UIState::View activeView;
};

constexpr float SILENCE_MAGNITUDE_THRESHOLD = 1e-5f;

bool spectrumIsSilent(const std::vector<float>& magnitudes) {
	for (float magnitude : magnitudes) {
		if (!std::isfinite(magnitude)) {
			continue;
		}
		if (std::fabs(magnitude) > SILENCE_MAGNITUDE_THRESHOLD) {
			return false;
		}
	}
	return true;
}

bool dropEventsContainSupportedImport(const std::vector<FileDropManager::FileDropEvent>& events) {
	for (const auto& event : events) {
		bool anySupported = std::any_of(
			event.paths.begin(),
			event.paths.end(),
			ReSyne::Recorder::isSupportedImportFile);
		if (anySupported) {
			return true;
		}
	}
	return false;
}

void sanitiseMagnitudes(std::vector<float>& magnitudes) {
	for (float& value : magnitudes) {
		if (!std::isfinite(value) || value < 0.0f) {
			value = 0.0f;
		}
	}
}

void resetAnalyserState(UIState& state, size_t binCount) {
	if (state.audioSettings.smoothedMagnitudes.size() != binCount) {
		state.audioSettings.smoothedMagnitudes.assign(binCount, 0.0f);
	} else {
		std::fill(state.audioSettings.smoothedMagnitudes.begin(),
				  state.audioSettings.smoothedMagnitudes.end(),
				  0.0f);
	}
	state.spectrumAnalyser.resetTemporalBuffers();
}

void applyColourSmoothing(float displayR, float displayG, float displayB,
						  float& outR, float& outG, float& outB,
						  const ColourUpdateContext& ctx) {
	if (ctx.smoothingEnabled) {
		ctx.colourSmoother.setTargetColour(displayR, displayG, displayB);
		ctx.colourSmoother.update(ctx.deltaTime * UIConstants::COLOUR_SMOOTH_UPDATE_FACTOR);

		if (ctx.activeView == UIState::View::ReSyne) {
			ctx.colourSmoother.getCurrentColour(outR, outG, outB);
		} else {
			ctx.colourSmoother.getCurrentColour(ctx.clearColour[0], ctx.clearColour[1], ctx.clearColour[2]);
			outR = ctx.clearColour[0];
			outG = ctx.clearColour[1];
			outB = ctx.clearColour[2];
		}
	} else {
		outR = displayR;
		outG = displayG;
		outB = displayB;
		if (ctx.activeView != UIState::View::ReSyne) {
			ctx.clearColour[0] = displayR;
			ctx.clearColour[1] = displayG;
			ctx.clearColour[2] = displayB;
		}
	}
}

void processPlaybackState(AudioInput& audioInput, UIState& state, ReSyne::RecorderState& recorderState,
						  float& currentDisplayR, float& currentDisplayG, float& currentDisplayB,
						  const ColourUpdateContext& ctx) {
	size_t playbackPosition = recorderState.audioOutput->getPlaybackPosition();
	size_t totalSamples = recorderState.audioOutput->getTotalSamples();

	if (totalSamples > 0 && recorderState.metadata.hopSize > 0 && !recorderState.samples.empty()) {
		size_t currentFrame = playbackPosition / static_cast<size_t>(recorderState.metadata.hopSize);
		currentFrame = std::min(currentFrame, recorderState.samples.size() - 1);

		recorderState.timeline.scrubberNormalisedPosition =
			static_cast<float>(currentFrame) / static_cast<float>(recorderState.samples.size() - 1);
	}

	ImVec4 playbackColour = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

	if (!recorderState.samples.empty()) {
		const float scrubberPos = std::clamp(recorderState.timeline.scrubberNormalisedPosition, 0.0f, 1.0f);

		std::lock_guard<std::mutex> lock(recorderState.samplesMutex);
		ReSyne::RecorderColourCache::ensureCacheLocked(recorderState);
		const float position = scrubberPos * (static_cast<float>(recorderState.samples.size()) - 1.0f);
		const size_t sampleIndex = static_cast<size_t>(position);
		const size_t clampedIndex = std::min(sampleIndex, recorderState.samples.size() - 1);
		const auto makeSample = [&](size_t idx) {
			ReSyne::Timeline::TimelineSample sample{};
			sample.timestamp = recorderState.samples[idx].timestamp;
			const auto& entry = recorderState.sampleColourCache[idx];
			sample.colour = entry.rgb;
			sample.labL = entry.labL;
			sample.labA = entry.labA;
			sample.labB = entry.labB;
			return sample;
		};
		if (recorderState.samples.size() == 1) {
			playbackColour = recorderState.sampleColourCache.front().rgb;
		} else {
			const float t = position - static_cast<float>(sampleIndex);
			playbackColour = ReSyne::Timeline::Gradient::interpolateColour(
				makeSample(sampleIndex),
				makeSample(std::min(sampleIndex + 1, recorderState.samples.size() - 1)),
				t,
				recorderState.importColourSpace);
		}

		const auto& currentSample = recorderState.samples[clampedIndex];

#ifdef ENABLE_API_SERVER
		auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
		auto colourResult = ColourMapper::spectrumToColour(
			currentSample.magnitudes,
			currentSample.phases,
			currentSample.sampleRate,
			UIConstants::DEFAULT_GAMMA,
			state.visualSettings.colourSpace,
			state.visualSettings.gamutMappingEnabled);
		api.updateColourData(currentSample.magnitudes, currentSample.phases, colourResult.dominantFrequency,
							 currentSample.sampleRate, currentDisplayR, currentDisplayG, currentDisplayB,
							 colourResult.L, colourResult.a, colourResult.b_comp);
#endif
		if (currentSample.magnitudes.empty()) {
			resetAnalyserState(state, 0);
		} else {
			const size_t binCount = currentSample.magnitudes.size();

			if (spectrumIsSilent(currentSample.magnitudes)) {
				resetAnalyserState(state, binCount);
			} else {
				if (state.audioSettings.smoothedMagnitudes.size() != binCount) {
					state.audioSettings.smoothedMagnitudes.assign(binCount, 0.0f);
				}

				std::vector<float> processedMagnitudes = currentSample.magnitudes;

				float maxMagnitude = 0.0f;
				for (float mag : processedMagnitudes) {
					if (!std::isfinite(mag)) {
						continue;
					}
					maxMagnitude = std::max(maxMagnitude, mag);
				}

				if (maxMagnitude <= SILENCE_MAGNITUDE_THRESHOLD) {
					resetAnalyserState(state, binCount);
				} else {
					float sampleRate = currentSample.sampleRate;
					if (sampleRate <= 0.0f) {
						sampleRate = recorderState.metadata.sampleRate;
					}
					if (sampleRate <= 0.0f) {
						sampleRate = recorderState.fallbackSampleRate;
					}
					if (sampleRate <= 0.0f) {
						sampleRate = UIConstants::DEFAULT_SAMPLE_RATE;
					}

					constexpr float MIN_FREQ = 20.0f;
					constexpr float MAX_FREQ = 20000.0f;
					constexpr size_t FFT_SIZE = 2048;

					const float normalisationFactor = 1.0f / maxMagnitude;
					const size_t minBinIndex = std::max(
						static_cast<size_t>(1),
						static_cast<size_t>(MIN_FREQ * FFT_SIZE / sampleRate));
					const size_t maxBinIndex = std::min(
						static_cast<size_t>(MAX_FREQ * FFT_SIZE / sampleRate) + 1,
						processedMagnitudes.size() - 1);

					if (minBinIndex <= maxBinIndex) {
						for (size_t i = minBinIndex; i <= maxBinIndex; ++i) {
							processedMagnitudes[i] *= normalisationFactor;
							const float freq = static_cast<float>(i) * sampleRate / FFT_SIZE;
							const float melWeight = FFTProcessor::calculateMelWeight(freq);
							processedMagnitudes[i] *= melWeight;
						}
					}

					static Equaliser playbackEqualiser;
					playbackEqualiser.setGains(
						state.audioSettings.lowGain,
						state.audioSettings.midGain,
						state.audioSettings.highGain);
					playbackEqualiser.applyEQ(processedMagnitudes, sampleRate, FFT_SIZE);

					sanitiseMagnitudes(processedMagnitudes);

					const float smoothing = std::clamp(state.audioSettings.spectrumSmoothingFactor, 0.0f, 1.0f);
					const float newContribution = 1.0f - smoothing;
					const float historyContribution = smoothing;

					for (size_t i = 0; i < processedMagnitudes.size(); ++i) {
						state.audioSettings.smoothedMagnitudes[i] =
							newContribution * processedMagnitudes[i] +
							historyContribution * state.audioSettings.smoothedMagnitudes[i];
					}
				}
			}
		}
	}

	applyColourSmoothing(playbackColour.x, playbackColour.y, playbackColour.z,
						 currentDisplayR, currentDisplayG, currentDisplayB, ctx);

	ctx.clearColour[0] = currentDisplayR;
	ctx.clearColour[1] = currentDisplayG;
	ctx.clearColour[2] = currentDisplayB;

	state.resyneState.displayColour[0] = currentDisplayR;
	state.resyneState.displayColour[1] = currentDisplayG;
	state.resyneState.displayColour[2] = currentDisplayB;

	ReSyne::updateFromFFT(
		state.resyneState,
		audioInput.getFFTProcessor(),
		audioInput.getSampleRate(),
		currentDisplayR,
		currentDisplayG,
		currentDisplayB);
}

void processLiveAudioState(AudioInput& audioInput, UIState& state, ReSyne::RecorderState& recorderState,
						   float& currentDisplayR, float& currentDisplayG, float& currentDisplayB,
						   const ColourUpdateContext& ctx) {
	constexpr float whiteMix = 0.0f;
	constexpr float gamma = 0.8f;

	recorderState.importGamma = gamma;
	recorderState.importColourSpace = state.visualSettings.colourSpace;
	recorderState.importGamutMapping = state.visualSettings.gamutMappingEnabled;
	ReSyne::RecorderColourCache::markSettingsIfChanged(
		recorderState,
		recorderState.importGamma,
		recorderState.importColourSpace,
		recorderState.importGamutMapping);
	recorderState.importLowGain = state.audioSettings.lowGain;
	recorderState.importMidGain = state.audioSettings.midGain;
	recorderState.importHighGain = state.audioSettings.highGain;

	audioInput.getFFTProcessor().setEQGains(state.audioSettings.lowGain, state.audioSettings.midGain, state.audioSettings.highGain);

	auto magnitudes = audioInput.getFFTProcessor().getMagnitudesBuffer();
	auto phases = audioInput.getFFTProcessor().getPhaseBuffer();

	const bool silentMagnitudeFrame = spectrumIsSilent(magnitudes);
	sanitiseMagnitudes(magnitudes);

	auto colourResult = ColourMapper::spectrumToColour(
		magnitudes,
		phases,
		audioInput.getSampleRate(),
		gamma,
		state.visualSettings.colourSpace,
		state.visualSettings.gamutMappingEnabled);

	const float adjustedR = colourResult.r * (1.0f - whiteMix) + whiteMix;
	const float adjustedG = colourResult.g * (1.0f - whiteMix) + whiteMix;
	const float adjustedB = colourResult.b * (1.0f - whiteMix) + whiteMix;

	const float displayR = std::clamp(adjustedR, 0.0f, 1.0f);
	const float displayG = std::clamp(adjustedG, 0.0f, 1.0f);
	const float displayB = std::clamp(adjustedB, 0.0f, 1.0f);

	bool currentValid = std::isfinite(ctx.clearColour[0]) && std::isfinite(ctx.clearColour[1]) &&
						std::isfinite(ctx.clearColour[2]);

	bool newValid = std::isfinite(displayR) && std::isfinite(displayG) && std::isfinite(displayB);

	if (!currentValid) {
		ctx.clearColour[0] = ctx.clearColour[1] = ctx.clearColour[2] = 0.1f;
	}

	if (newValid) {
		applyColourSmoothing(displayR, displayG, displayB,
							currentDisplayR, currentDisplayG, currentDisplayB, ctx);
	}

#ifdef ENABLE_API_SERVER
	auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
	api.updateColourData(magnitudes, phases, colourResult.dominantFrequency, audioInput.getSampleRate(),
						 currentDisplayR, currentDisplayG, currentDisplayB,
						 colourResult.L, colourResult.a, colourResult.b_comp);
#endif

	ReSyne::updateFromFFT(
		state.resyneState,
		audioInput.getFFTProcessor(),
		audioInput.getSampleRate(),
		currentDisplayR,
		currentDisplayG,
		currentDisplayB);

	if (silentMagnitudeFrame || magnitudes.empty()) {
		resetAnalyserState(state, magnitudes.size());
	} else {
		if (state.audioSettings.smoothedMagnitudes.size() != magnitudes.size()) {
			state.audioSettings.smoothedMagnitudes.assign(magnitudes.size(), 0.0f);
		}

		const float smoothing = std::clamp(state.audioSettings.spectrumSmoothingFactor, 0.0f, 1.0f);
		const float newContribution = 1.0f - smoothing;
		const float historyContribution = smoothing;

		for (size_t i = 0; i < magnitudes.size(); ++i) {
			state.audioSettings.smoothedMagnitudes[i] =
				newContribution * magnitudes[i] +
				historyContribution * state.audioSettings.smoothedMagnitudes[i];
		}
	}
}

void processIdleState(float* clearColour, float deltaTime) {
	UITheme currentTheme = SystemThemeDetector::isSystemInDarkMode() ? UITheme::Dark : UITheme::Light;
	float targetColor[3];
	if (currentTheme == UITheme::Light) {
		targetColor[0] = 1.0f;
		targetColor[1] = 1.0f;
		targetColor[2] = 1.0f;
	} else {
		targetColor[0] = 0.0f;
		targetColor[1] = 0.0f;
		targetColor[2] = 0.0f;
	}

	float decayFactor = std::min(1.0f, deltaTime * UIConstants::COLOUR_DECAY_RATE);
	clearColour[0] = std::clamp(clearColour[0] * (1.0f - decayFactor) + targetColor[0] * decayFactor, 0.0f, 1.0f);
	clearColour[1] = std::clamp(clearColour[1] * (1.0f - decayFactor) + targetColor[1] * decayFactor, 0.0f, 1.0f);
	clearColour[2] = std::clamp(clearColour[2] * (1.0f - decayFactor) + targetColor[2] * decayFactor, 0.0f, 1.0f);
}

void handleFileDropEvents(const std::vector<FileDropManager::FileDropEvent>& dropEvents,
						  ReSyne::RecorderState& recorderState) {
	if (dropEvents.empty()) {
		return;
	}

	for (const auto& event : dropEvents) {
		bool anySupported = std::any_of(event.paths.begin(), event.paths.end(),
										ReSyne::Recorder::isSupportedImportFile);
		if (!anySupported) {
			continue;
		}

		bool accepted = false;
		bool attempted = false;
		if (recorderState.timeline.gradientRegionValid) {
			const float dropX = static_cast<float>(event.cursorX);
			const float dropY = static_cast<float>(event.cursorY);
			bool inside = dropX >= recorderState.timeline.gradientRegionMin.x &&
			              dropX <= recorderState.timeline.gradientRegionMax.x &&
			              dropY >= recorderState.timeline.gradientRegionMin.y &&
			              dropY <= recorderState.timeline.gradientRegionMax.y;
			if (inside) {
				for (const auto& path : event.paths) {
					if (!ReSyne::Recorder::isSupportedImportFile(path)) {
						continue;
					}
					attempted = true;
					recorderState.pendingImportPath = path;
					recorderState.importPhase = 1;
					accepted = true;
					break;
				}
			}
		}

		if (!accepted && !attempted) {
			recorderState.statusMessage = "Drop supported files onto the ReSyne gradient to import";
			recorderState.statusMessageTimer = 2.5f;
		}
	}
}

}

void updateUI(AudioInput& audioInput, const std::vector<AudioInput::DeviceInfo>& devices,
			  const std::vector<AudioOutput::DeviceInfo>& outputDevices,
			  float* clear_colour, ImGuiIO& io, UIState& state
#ifdef ENABLE_MIDI
			  , MIDIInput* midiInput,
			  std::vector<MIDIInput::DeviceInfo>* midiDevices
#endif
			  ) {
	initialiseApp(state);
	state.updateChecker.update(state.updateState);

#ifdef ENABLE_MIDI
	UIHandlers::MIDIHandler::update(state, io.DeltaTime, midiInput, midiDevices);
#endif

	auto dropEvents = FileDropManager::consume();
	if (!dropEvents.empty() &&
	    state.visualSettings.activeView == UIState::View::Visualisation &&
	    dropEventsContainSupportedImport(dropEvents)) {
		state.visualSettings.activeView = UIState::View::ReSyne;
	}
	ReSyne::Timeline::TrackpadGestureInput trackpadInput;
	{
		auto gestures = TrackpadGestures::consume();
		if (gestures.hasPinch) {
			trackpadInput.hasPinch = true;
			trackpadInput.pinchDelta = static_cast<float>(gestures.pinchDelta);
		}
		if (gestures.hasHorizontalPan) {
			trackpadInput.hasHorizontalPan = true;
			trackpadInput.panDeltaX = static_cast<float>(gestures.panDeltaX);
		}
		if (gestures.pointerValid) {
			if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
				trackpadInput.pointerValid = true;
				trackpadInput.pointerPos = ImVec2(
					static_cast<float>(gestures.cursorX) + viewport->Pos.x,
					static_cast<float>(gestures.cursorY) + viewport->Pos.y
				);
			}
		}
		if (!gestures.pointerValid) {
			trackpadInput.mouseWheelDelta = io.MouseWheel;
		} else {
			trackpadInput.mouseWheelDelta = 0.0f;
		}
	}
	auto& recorderState = state.resyneState.recorderState;
	recorderState.trackpadInput = trackpadInput;
	recorderState.timeline.gradientRegionValid = false;
	recorderState.timeline.hoverOverlayAlpha =
	    std::max(0.0f, recorderState.timeline.hoverOverlayAlpha - io.DeltaTime * 3.0f);
	recorderState.dropFlashAlpha = std::max(0.0f, recorderState.dropFlashAlpha - io.DeltaTime * 2.0f);
	if (recorderState.statusMessageTimer > 0.0f) {
		recorderState.statusMessageTimer = std::max(0.0f, recorderState.statusMessageTimer - io.DeltaTime);
		if (recorderState.statusMessageTimer <= 0.0f) {
			recorderState.statusMessage.clear();
		}
	}

	UIHandlers::ImportHandler::processFileImport(recorderState);

	if (ImGui::IsKeyPressed(ImGuiKey_H) && state.visualSettings.activeView != UIState::View::ReSyne) {
		state.visibility.showUI = !state.visibility.showUI;
	}

	auto toggleActiveView = [&state]() {
		state.visualSettings.activeView = (state.visualSettings.activeView == UIState::View::ReSyne)
		                       ? UIState::View::Visualisation
		                       : UIState::View::ReSyne;
	};

	if (state.visibility.showUI && ImGui::IsKeyPressed(ImGuiKey_Tab, false) && io.KeyShift) {
		toggleActiveView();
	}

    DeviceManager::populateDeviceNames(state.deviceState, devices);
    DeviceManager::populateOutputDeviceNames(state.deviceState, outputDevices);

    const int previousOutputDeviceIndex = recorderState.outputDeviceIndex;
    int fallbackOutputDeviceSelection = -1;
    if (previousOutputDeviceIndex >= 0) {
        auto fallbackIt = std::find_if(
            outputDevices.begin(),
            outputDevices.end(),
            [previousOutputDeviceIndex](const AudioOutput::DeviceInfo& device) {
                return device.paIndex == previousOutputDeviceIndex;
            });
        if (fallbackIt != outputDevices.end()) {
            fallbackOutputDeviceSelection = static_cast<int>(std::distance(outputDevices.begin(), fallbackIt));
        }
    }

    int requestedOutputDeviceIndex = -1;
    if (state.deviceState.selectedOutputDeviceIndex >= 0 &&
        static_cast<size_t>(state.deviceState.selectedOutputDeviceIndex) < outputDevices.size()) {
        requestedOutputDeviceIndex =
            outputDevices[static_cast<size_t>(state.deviceState.selectedOutputDeviceIndex)].paIndex;
    }

    recorderState.outputDeviceIndex = requestedOutputDeviceIndex;

    const bool outputDeviceChanged = requestedOutputDeviceIndex != previousOutputDeviceIndex;
    if (outputDeviceChanged && recorderState.audioOutput) {
        const bool wasPlaying = recorderState.audioOutput->isPlaying();
        const size_t playbackPosition = recorderState.audioOutput->getPlaybackPosition();

        recorderState.audioOutput->pause();

        float sampleRate = recorderState.metadata.sampleRate;
        if (sampleRate <= 0.0f) {
            sampleRate = recorderState.fallbackSampleRate;
        }
        if (sampleRate <= 0.0f) {
            sampleRate = UIConstants::DEFAULT_SAMPLE_RATE;
        }

        if (!recorderState.audioOutput->initOutputStream(sampleRate, requestedOutputDeviceIndex)) {
            recorderState.outputDeviceIndex = previousOutputDeviceIndex;
            state.deviceState.selectedOutputDeviceIndex = fallbackOutputDeviceSelection;
            recorderState.statusMessage = "Unable to switch output device";
            recorderState.statusMessageTimer = 4.0f;
        } else {
            recorderState.audioOutput->seek(playbackPosition);
            if (wasPlaying) {
                recorderState.audioOutput->play();
            }
        }
    }

	float deltaTime = io.DeltaTime;
	static SpringSmoother colourSmoother(8.0f, 1.0f, 0.3f);
	colourSmoother.setSmoothingAmount(state.visualSettings.colourSmoothingSpeed);

	constexpr float SIDEBAR_WIDTH = 280.0f;
	constexpr float SIDEBAR_PADDING = 12.0f;
	constexpr float contentWidth = SIDEBAR_WIDTH - SIDEBAR_PADDING * 2;

	if (state.visibility.showUI && state.updateChecker.shouldShowUpdateBanner(state.updateState)) {
        state.updateChecker.drawUpdateBanner(state.updateState, io.DisplaySize.x, SIDEBAR_WIDTH);
    }
    
	static float currentDisplayR = 0.0f;
	static float currentDisplayG = 0.0f;
	static float currentDisplayB = 0.0f;

	bool hasPlaybackData = !recorderState.samples.empty() &&
	                       recorderState.audioOutput &&
	                       recorderState.audioOutput->isPlaying();

	ColourUpdateContext colourCtx{
		deltaTime,
		state.visualSettings.smoothingEnabled,
		colourSmoother,
		clear_colour,
		state.visualSettings.activeView
	};

	if (hasPlaybackData) {
		processPlaybackState(audioInput, state, recorderState, currentDisplayR, currentDisplayG, currentDisplayB, colourCtx);
	}

	bool hasMicInput = state.deviceState.selectedDeviceIndex >= 0;

	if (!hasPlaybackData && hasMicInput) {
		processLiveAudioState(audioInput, state, recorderState, currentDisplayR, currentDisplayG, currentDisplayB, colourCtx);
	}

	if (!hasPlaybackData && !hasMicInput) {
		processIdleState(clear_colour, deltaTime);
	}

	if (state.visibility.showUI) {
		ImGuiStyle& style = ImGui::GetStyle();

		UITheme currentTheme = SystemThemeDetector::isSystemInDarkMode() ? UITheme::Dark : UITheme::Light;
		UIStyler::applyCustomStyleWithTheme(style, state.styleState, currentTheme);

		ImVec2 displaySize = io.DisplaySize;

		constexpr float RECORDER_PANEL_HEIGHT = 200.0f;

		bool showRecorder = state.deviceState.selectedDeviceIndex >= 0 &&
		                    !state.deviceState.streamError &&
		                    state.resyneState.recorderState.windowOpen;

		float bottomPanelHeight = showRecorder ? RECORDER_PANEL_HEIGHT : 0.0f;

		constexpr float BUTTON_HEIGHT = 25.0f;
		constexpr float CONTROL_WIDTH = 130.0f;
		constexpr float LABEL_WIDTH = 90.0f;

		Sidebar::LayoutMetrics sidebarLayout{
			SIDEBAR_WIDTH,
			SIDEBAR_PADDING,
			LABEL_WIDTH,
			CONTROL_WIDTH,
			BUTTON_HEIGHT,
			contentWidth
		};

		Sidebar::RenderArgs sidebarArgs{
			audioInput,
			devices,
			outputDevices,
			clear_colour,
			state,
			recorderState,
			colourSmoother,
			sidebarLayout,
			displaySize,
			hasPlaybackData
#ifdef ENABLE_MIDI
			, midiInput
			, midiDevices
#endif
		};

		Sidebar::render(sidebarArgs);

		bool showSpectrum = (state.deviceState.selectedDeviceIndex >= 0 && !state.deviceState.streamError) || hasPlaybackData;

		bool showSpectrumInCurrentView = state.visualSettings.activeView == UIState::View::Visualisation;

		if (showSpectrum &&
		    state.visibility.showSpectrumAnalyser &&
		    showSpectrumInCurrentView) {
			state.spectrumAnalyser.drawSpectrumWindow(
				state.audioSettings.smoothedMagnitudes,
				devices,
				state.deviceState.selectedDeviceIndex,
				displaySize,
				SIDEBAR_WIDTH,
				state.visibility.sidebarOnLeft,
				bottomPanelHeight
			);
		}

		if (state.visualSettings.activeView == UIState::View::ReSyne) {
			float windowX = state.visibility.sidebarOnLeft ? SIDEBAR_WIDTH : 0.0f;
			float windowY = 0.0f;
			float windowWidth = displaySize.x - SIDEBAR_WIDTH;
			float windowHeight = displaySize.y;

			ReSyne::renderMainView(
				state.resyneState,
				audioInput,
				audioInput.getFFTProcessor(),
				windowX,
				windowY,
				windowWidth,
				windowHeight);
		} else if (showRecorder) {
			float panelX = state.visibility.sidebarOnLeft ? SIDEBAR_WIDTH : 0.0f;
			float panelWidth = displaySize.x - SIDEBAR_WIDTH;
			float panelY = displaySize.y - RECORDER_PANEL_HEIGHT;

			ReSyne::renderRecorderPanel(
				state.resyneState,
				audioInput.getFFTProcessor(),
				panelX,
				panelY,
				panelWidth,
				RECORDER_PANEL_HEIGHT);
		}

			ReSyne::handleDialogs(state.resyneState);
		}

	handleFileDropEvents(dropEvents, recorderState);

	UIStyler::restoreOriginalStyle(ImGui::GetStyle(), state.styleState);
}
