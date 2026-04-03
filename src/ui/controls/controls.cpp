#include "controls.h"

#include <imgui.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour_mapper.h"
#include "ui.h"
#include "resyne/recorder/recorder.h"
#ifdef ENABLE_OSC
#include "synesthesia_osc_integration.h"
#endif
#ifdef ENABLE_MIDI
#include "midi_device_manager.h"
#endif

namespace {

void renderWrappedStatusText(const char* text, const ImVec4* colour = nullptr) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
    if (colour != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, *colour);
    }
    ImGui::TextUnformatted(text);
    if (colour != nullptr) {
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
}

}

namespace Controls {

void renderFrequencyInfoPanel(AudioInput& audioInput, float* clear_colour, const UIState& state, ReSyne::RecorderState& recorderState) {
    if (ImGui::CollapsingHeader("Output Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10);

        const auto settings = SpectralPresentation::Settings{
            state.audioSettings.lowGain,
            state.audioSettings.midGain,
            state.audioSettings.highGain,
            UIConstants::DEFAULT_GAMMA,
            state.visualSettings.colourSpace,
            state.visualSettings.gamutMappingEnabled
        };
        const bool hasPlaybackSession =
            recorderState.audioOutput != nullptr &&
            recorderState.audioOutput->getTotalFrames() > 0 &&
            !recorderState.samples.empty();

        SpectralPresentation::Frame frame{};
		float loudnessOverride = ColourMapper::LOUDNESS_DB_UNSPECIFIED;
        if (hasPlaybackSession) {
            std::lock_guard<std::mutex> lock(recorderState.samplesMutex);
            const float position = std::clamp(recorderState.timeline.scrubberNormalisedPosition, 0.0f, 1.0f) *
                                   (static_cast<float>(recorderState.samples.size()) - 1.0f);
            const size_t sampleIndex = static_cast<size_t>(position);
            const size_t clampedIndex = std::min(sampleIndex, recorderState.samples.size() - 1);

            const auto& currentSample = recorderState.samples[clampedIndex];
            frame = SpectralPresentation::mixChannels(
                currentSample.magnitudes,
                currentSample.phases,
                currentSample.frequencies,
                currentSample.channels,
                currentSample.sampleRate);
			if (std::isfinite(currentSample.loudnessLUFS)) {
				loudnessOverride = currentSample.loudnessLUFS;
			}
        } else {
            const auto spectralData = audioInput.getSpectralData();
            frame = SpectralPresentation::mixChannels(
                spectralData.magnitudes,
                spectralData.phases,
                {},
                static_cast<std::uint32_t>(spectralData.magnitudes.size()),
                spectralData.sampleRate > 0.0f ? spectralData.sampleRate : audioInput.getSampleRate());
			loudnessOverride = audioInput.getFFTProcessor().getMomentaryLoudnessLUFS();
        }

		auto currentColourResult = SpectralPresentation::buildColourResult(
            frame,
            settings,
            loudnessOverride);

		const bool hasFiniteLoudness = std::isfinite(currentColourResult.loudnessDb);
		if (currentColourResult.dominantFrequency > 0.0f) {
			ImGui::Text("Spectral centroid: %.1f Hz", static_cast<double>(currentColourResult.dominantFrequency));
			ImGui::Text("Wavelength: %.1f nm", static_cast<double>(currentColourResult.dominantWavelength));
			ImGui::Text("RGB: (%.2f, %.2f, %.2f)", static_cast<double>(clear_colour[0]), static_cast<double>(clear_colour[1]), static_cast<double>(clear_colour[2]));
			if (hasFiniteLoudness) {
				ImGui::Text("Loudness: %.1f LUFS", static_cast<double>(currentColourResult.loudnessDb));
			} else {
				ImGui::TextDisabled("Loudness: -- LUFS");
			}
		} else {
			ImGui::TextDisabled("No significant frequencies");
		}

        ImGui::Unindent(10);
        ImGui::Spacing();
    }
}

void renderVisualiserSettingsPanel(SpringSmoother& colourSmoother, 
                                 float& smoothingAmount,
                                 bool& manualSmoothing,
                                 bool& showSpectrumAnalyser,
                                 float sidebarWidth,
                                 float sidebarPadding,
                                 float labelWidth,
                                 float controlWidth,
                                 float buttonHeight) {
    if (ImGui::CollapsingHeader("Smoothing")) {
        ImGui::Indent(10);
        const float contentWidth = sidebarWidth - sidebarPadding * 2.0f;
        const float buttonWidth = (contentWidth - ImGui::GetStyle().ItemSpacing.x) / 2.0f;

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Manual Smoothing");
        ImGui::SameLine();
        ImGui::SetCursorPosX(sidebarPadding + contentWidth - ImGui::GetFrameHeight());
        const bool previousManualSmoothing = manualSmoothing;
        ImGui::Checkbox("##ManualSmoothing", &manualSmoothing);
        if (manualSmoothing != previousManualSmoothing) {
            if (!manualSmoothing) {
                smoothingAmount = UIConstants::DEFAULT_SMOOTHING_SPEED;
                colourSmoother.setSmoothingAmount(smoothingAmount);
            }
            float r, g, b;
            colourSmoother.getCurrentColour(r, g, b);
            colourSmoother.reset(r, g, b);
        }

        if (manualSmoothing) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Smoothing");
            ImGui::SameLine(sidebarPadding + labelWidth);
            ImGui::SetCursorPosX(sidebarWidth - sidebarPadding - controlWidth);
            ImGui::SetNextItemWidth(controlWidth);
            if (ImGui::SliderFloat("##Smoothing", &smoothingAmount, 0.0f, 1.0f, "%.2f")) {
                colourSmoother.setSmoothingAmount(smoothingAmount);
            }

            ImGui::SetCursorPosX(sidebarPadding);
            if (ImGui::Button("Reset Smoothing", ImVec2(buttonWidth, buttonHeight))) {
                smoothingAmount = UIConstants::DEFAULT_SMOOTHING_SPEED;
                colourSmoother.setSmoothingAmount(smoothingAmount);
            }
            ImGui::SameLine();
            ImGui::SetCursorPosX(sidebarPadding + buttonWidth + ImGui::GetStyle().ItemSpacing.x);
        } else {
            ImGui::SetCursorPosX(sidebarPadding);
        }

        if (ImGui::Button(showSpectrumAnalyser ? "Hide Spectrum" : "Show Spectrum",
                          ImVec2(manualSmoothing ? buttonWidth : contentWidth, buttonHeight))) {
            showSpectrumAnalyser = !showSpectrumAnalyser;
        }

        ImGui::Unindent(10);
        ImGui::Spacing();
    }
}

void renderEQControlsPanel(float& lowGain,
                          float& midGain,
                          float& highGain,
                          float sidebarWidth,
                          float sidebarPadding,
                          float labelWidth,
                          float controlWidth,
                          float buttonHeight,
                          float contentWidth) {
    if (ImGui::CollapsingHeader("EQ/Gain")) {
        ImGui::Indent(10);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Lows");
        ImGui::SameLine(sidebarPadding + labelWidth);
        ImGui::SetCursorPosX(sidebarWidth - sidebarPadding - controlWidth);
        ImGui::SetNextItemWidth(controlWidth);
        ImGui::SliderFloat("##LowGain", &lowGain, 0.0f, 2.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Mids");
        ImGui::SameLine(sidebarPadding + labelWidth);
        ImGui::SetCursorPosX(sidebarWidth - sidebarPadding - controlWidth);
        ImGui::SetNextItemWidth(controlWidth);
        ImGui::SliderFloat("##MidGain", &midGain, 0.0f, 2.0f);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Highs");
        ImGui::SameLine(sidebarPadding + labelWidth);
        ImGui::SetCursorPosX(sidebarWidth - sidebarPadding - controlWidth);
        ImGui::SetNextItemWidth(controlWidth);
        ImGui::SliderFloat("##HighGain", &highGain, 0.0f, 2.0f);

        ImGui::SetCursorPosX(sidebarPadding);
        if (ImGui::Button("Reset EQ", ImVec2(contentWidth, buttonHeight))) {
            lowGain = midGain = highGain = 1.0f;
        }

        ImGui::Unindent(10);
    }
}

void renderAdvancedSettingsPanel(UIState& state
#ifdef ENABLE_MIDI
                                  , MIDIInput* midiInput
                                  , const std::vector<MIDIInput::DeviceInfo>* midiDevices
#endif
                                  ) {
    static bool previousSmoothingState = state.visualSettings.smoothingEnabled;
    
    if (ImGui::CollapsingHeader("Advanced Settings")) {
        if (ImGui::CollapsingHeader("Appearance")) {
			ImGui::Indent(10);
            ImGui::Text("Sidebar: %s", state.visibility.sidebarOnLeft ? "Left" : "Right");
            if (ImGui::Button("Swap Sides")) {
                state.visibility.sidebarOnLeft = !state.visibility.sidebarOnLeft;
            }
            
            ImGui::Spacing();
            bool currentSmoothingState = state.visualSettings.smoothingEnabled;
            if (ImGui::Checkbox("Enable Smoothing", &currentSmoothingState)) {
                if (currentSmoothingState != state.visualSettings.smoothingEnabled) {
                    if (!currentSmoothingState && state.visualSettings.smoothingEnabled) {
                        previousSmoothingState = state.visualSettings.smoothingEnabled;
                        ImGui::OpenPopup("Photosensitivity Warning");
                    } else {
                        state.visualSettings.smoothingEnabled = currentSmoothingState;
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Smoothing reduces rapid colour changes.\nDisabling will cause rapid flashing.");
            }
            
            if (ImGui::BeginPopupModal("Photosensitivity Warning", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("Warning: Disabling smoothing will cause rapidly flashing colours which can trigger photosensitive epilepsy in sensitive individuals.");
                ImGui::Spacing();
                ImGui::TextWrapped("Are you sure you want to disable smoothing?");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                
                if (ImGui::Button("(Yes) Disable Smoothing")) {
                    state.visualSettings.smoothingEnabled = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("(No) Keep Smoothing Enabled")) {
                    state.visualSettings.smoothingEnabled = previousSmoothingState;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            int colourSpaceIndex = 0;
            switch (state.visualSettings.colourSpace) {
                case ColourMapper::ColourSpace::Rec2020:
                    colourSpaceIndex = 0;
                    break;
                case ColourMapper::ColourSpace::DisplayP3:
                    colourSpaceIndex = 1;
                    break;
                case ColourMapper::ColourSpace::SRGB:
                    colourSpaceIndex = 2;
                    break;
            }
            const char* colourSpaceLabels[] = {"Rec.2020", "Display P3", "sRGB"};
            ImGui::Text("Working Colour Space");
            if (ImGui::Combo("##WorkingColourSpace", &colourSpaceIndex, colourSpaceLabels, IM_ARRAYSIZE(colourSpaceLabels))) {
                switch (colourSpaceIndex) {
                    case 0:
                        state.visualSettings.colourSpace = ColourMapper::ColourSpace::Rec2020;
                        break;
                    case 1:
                        state.visualSettings.colourSpace = ColourMapper::ColourSpace::DisplayP3;
                        break;
                    case 2:
                    default:
                        state.visualSettings.colourSpace = ColourMapper::ColourSpace::SRGB;
                        break;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Selects the working colour space used for analysis, playback, and exports.");
            }

            ImGui::Checkbox("Apply Gamut Mapping", &state.visualSettings.gamutMappingEnabled);

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, colours are compressed into the display gamut.\nDisable to preserve raw values for wide-gamut workflows.");
            }

			ImGui::Unindent(10);
        }
        
#ifdef ENABLE_OSC
        if (ImGui::CollapsingHeader("OSC")) {
			ImGui::Indent(10);
            auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
            const auto stats = osc.getStats();
            const bool transportRunning = osc.isRunning();
            const Synesthesia::OSC::OSCConfig currentConfig = osc.getConfig();
            const auto destinationValidation = Synesthesia::OSC::validateOSCDestination(state.oscSettings.destinationHost);

	            ImGui::Text("Destination");
	            std::array<char, 16> destinationHost{};
	            std::snprintf(destinationHost.data(), destinationHost.size(), "%s", state.oscSettings.destinationHost.c_str());
	            if (ImGui::InputText("##OSCDestination", destinationHost.data(), destinationHost.size())) {
	                state.oscSettings.destinationHost = destinationHost.data();
	            }
	            if (!destinationValidation.valid) {
	                const ImVec4 errorColour(1.0f, 0.3f, 0.3f, 1.0f);
	                renderWrappedStatusText(destinationValidation.errorMessage.c_str(), &errorColour);
	            } else {
	                renderWrappedStatusText("Loopback or RFC1918 private IPv4 only");
	            }

	            ImGui::Text("Transmit Port");
	            ImGui::InputInt("##OSCTransmitPort", &state.oscSettings.transmitPort);

	            ImGui::Text("Receive Port");
	            ImGui::InputInt("##OSCReceivePort", &state.oscSettings.receivePort);

	            state.oscSettings.transmitPort = std::clamp(state.oscSettings.transmitPort, 1, 65535);
	            state.oscSettings.receivePort = std::clamp(state.oscSettings.receivePort, 1, 65535);

	            ImGui::Spacing();
            ImGui::Text("Transport Status: %s", transportRunning ? "Running" : "Stopped");
            ImGui::Text("Frames Sent: %llu", static_cast<unsigned long long>(stats.framesSent));
            ImGui::Text("Messages Received: %llu", static_cast<unsigned long long>(stats.messagesReceived));

            const std::string lastError = osc.getLastError();
            if (!lastError.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", lastError.c_str());
            }
            
            if (transportRunning) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Performance");
                ImGui::Spacing();
                
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);

                ImGui::Text("FPS: %u", stats.currentFps);
                if (stats.averageSendTimeMs > 0.0f) {
                    ImGui::Text("Average Send Time: %.2fms", static_cast<double>(stats.averageSendTimeMs));
                }
                ImGui::PopTextWrapPos();
                ImGui::Separator();
            }
            
            ImGui::Spacing();
            const std::string desiredDestination = destinationValidation.valid
                ? destinationValidation.canonicalHost
                : state.oscSettings.destinationHost;
            const bool hasPendingConfigChanges =
                desiredDestination != currentConfig.destinationHost ||
                static_cast<uint16_t>(state.oscSettings.transmitPort) != currentConfig.transmitPort ||
                static_cast<uint16_t>(state.oscSettings.receivePort) != currentConfig.receivePort;

            if (transportRunning && hasPendingConfigChanges) {
                ImGui::BeginDisabled(!destinationValidation.valid);
                if (ImGui::Button("Apply Settings", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                    Synesthesia::OSC::OSCConfig config;
                    config.destinationHost = state.oscSettings.destinationHost;
                    config.transmitPort = static_cast<uint16_t>(state.oscSettings.transmitPort);
                    config.receivePort = static_cast<uint16_t>(state.oscSettings.receivePort);
                    osc.updateConfig(config);
                    state.oscEnabled = osc.isRunning();
                    state.oscSettings.destinationHost = osc.getConfig().destinationHost;
                }
                ImGui::EndDisabled();
                ImGui::Spacing();
            }

            const float buttonWidth = ImGui::GetContentRegionAvail().x;
            ImGui::BeginDisabled(!transportRunning && !destinationValidation.valid);
            if (ImGui::Button(transportRunning ? "Disable" : "Enable", ImVec2(buttonWidth, 0))) {
                if (transportRunning) {
                    state.oscEnabled = false;
                    osc.stop();
                } else {
                    Synesthesia::OSC::OSCConfig config;
                    config.destinationHost = state.oscSettings.destinationHost;
                    config.transmitPort = static_cast<uint16_t>(state.oscSettings.transmitPort);
                    config.receivePort = static_cast<uint16_t>(state.oscSettings.receivePort);
                    state.oscEnabled = osc.start(config);
                    state.oscSettings.destinationHost = osc.getConfig().destinationHost;
                }
            }
            ImGui::EndDisabled();

			ImGui::Unindent(10);
        }
#endif

#ifdef ENABLE_MIDI
        if (midiInput && midiDevices) {
            if (ImGui::CollapsingHeader("MIDI")) {
                ImGui::Indent(10);
                ImGui::Text("MIDI INPUT DEVICE");
                MIDIDeviceManager::renderMIDIDeviceSelection(state.midiDeviceState, *midiInput, *midiDevices);

                ImGui::Unindent(10);
            }
        }
#endif
    }
}

}
