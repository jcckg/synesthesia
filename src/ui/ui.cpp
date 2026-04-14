#include "ui.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "audio_input.h"
#include "controls.h"
#include "colour/colour_core.h"
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
#include "ui/audio_visualisation/presentation_state.h"
#include "ui/audio_visualisation/visualisation_surface.h"
#include "ui/dragdrop/file_drop_manager.h"
#include "ui/input/trackpad_gestures.h"
#include "system_theme_detector.h"
#include "ui/handlers/import_handler.h"
#ifdef ENABLE_MIDI
#include "ui/handlers/midi_handler.h"
#endif
#ifdef ENABLE_OSC
#include "synesthesia_osc_integration.h"
#endif

void initialiseApp(UIState& state) {
    if (!state.updateState.hasCheckedThisSession) {
        state.updateChecker.checkForUpdates("jcckg", "synesthesia");
        state.updateState.hasCheckedThisSession = true;
    }
    
#ifdef ENABLE_OSC
    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    if (osc.isRunning() && !state.oscEnabled) {
        osc.stop();
    }
#endif
}

namespace {

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

#ifdef ENABLE_OSC
    {
        auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
        const auto pendingSettings = osc.consumePendingSettings();
        if (pendingSettings.smoothingEnabled.has_value()) {
            state.visualSettings.smoothingEnabled = *pendingSettings.smoothingEnabled;
        }
        if (pendingSettings.colourSmoothingSpeed.has_value()) {
            state.visualSettings.colourSmoothingSpeed = *pendingSettings.colourSmoothingSpeed;
        }
        if (pendingSettings.spectrumSmoothingAmount.has_value()) {
            state.audioSettings.spectrumSmoothingFactor = *pendingSettings.spectrumSmoothingAmount;
        }
        if (pendingSettings.colourSpace.has_value()) {
            state.visualSettings.colourSpace = *pendingSettings.colourSpace;
        }
        if (pendingSettings.gamutMappingEnabled.has_value()) {
            state.visualSettings.gamutMappingEnabled = *pendingSettings.gamutMappingEnabled;
        }
    }
#endif

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
	if (recorderState.detachedVisualisation.isOpen &&
	    state.visualSettings.activeView == UIState::View::Visualisation) {
		state.visualSettings.activeView = UIState::View::ReSyne;
		state.visibility.showUI = true;
	}
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

	if (!recorderState.detachedVisualisation.isOpen &&
	    state.visibility.showUI &&
	    ImGui::IsKeyPressed(ImGuiKey_Tab, false) &&
	    io.KeyShift) {
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
        const int channelCount = recorderState.metadata.channels > 0 ? static_cast<int>(recorderState.metadata.channels) : 1;

        if (!recorderState.audioOutput->initOutputStream(sampleRate, channelCount, requestedOutputDeviceIndex)) {
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
	state.audioSettings.spectrumSmoothingFactor = state.visualSettings.colourSmoothingSpeed;
	UI::AudioVisualisation::syncRecorderPresentationSettings(state);

	constexpr float SIDEBAR_WIDTH = 280.0f;
	constexpr float SIDEBAR_PADDING = 12.0f;
	constexpr float contentWidth = SIDEBAR_WIDTH - SIDEBAR_PADDING * 2;

	if (state.visibility.showUI && state.updateChecker.shouldShowUpdateBanner(state.updateState)) {
        state.updateChecker.drawUpdateBanner(state.updateState, io.DisplaySize.x, SIDEBAR_WIDTH);
    }
    
	static float currentDisplayR = 0.0f;
	static float currentDisplayG = 0.0f;
	static float currentDisplayB = 0.0f;

	bool hasPlaybackSession = UI::AudioVisualisation::hasPlaybackSession(recorderState);

	UI::AudioVisualisation::ColourUpdateContext colourCtx{
		deltaTime,
		state.visualSettings.smoothingEnabled,
		state.visualSettings.manualSmoothing,
		colourSmoother,
		clear_colour,
		state.visualSettings.activeView
	};

	if (hasPlaybackSession) {
		UI::AudioVisualisation::processPlaybackState(
			audioInput,
			state,
			recorderState,
			currentDisplayR,
			currentDisplayG,
			currentDisplayB,
			colourCtx);
	}

	bool hasMicInput = state.deviceState.selectedDeviceIndex >= 0;

	if (!hasPlaybackSession && hasMicInput) {
		UI::AudioVisualisation::processLiveAudioState(
			audioInput,
			state,
			recorderState,
			currentDisplayR,
			currentDisplayG,
			currentDisplayB,
			colourCtx);
	}

	if (!hasPlaybackSession && !hasMicInput) {
		processIdleState(clear_colour, deltaTime);
		currentDisplayR = clear_colour[0];
		currentDisplayG = clear_colour[1];
		currentDisplayB = clear_colour[2];
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
			hasPlaybackSession
#ifdef ENABLE_MIDI
			, midiInput
			, midiDevices
#endif
		};

		Sidebar::render(sidebarArgs);

		if (state.visualSettings.activeView == UIState::View::Visualisation) {
			UI::AudioVisualisation::SurfaceLayout layout;
			layout.displaySize = displaySize;
			layout.sidebarWidth = SIDEBAR_WIDTH;
			layout.sidebarOnLeft = state.visibility.sidebarOnLeft;
			layout.bottomPanelHeight = bottomPanelHeight;
			UI::AudioVisualisation::renderSpectrumOverlay(
				state,
				audioInput,
				layout,
				hasPlaybackSession
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
				audioInput.getAudioProcessor(),
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
