#include "controls.h"

#include <imgui.h>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cmath>

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

void sanitiseMagnitudes(std::vector<float>& magnitudes) {
	for (float& value : magnitudes) {
		if (!std::isfinite(value) || value < 0.0f) {
			value = 0.0f;
		}
	}
}

}

namespace Controls {

void renderFrequencyInfoPanel(AudioInput& audioInput, float* clear_colour, const UIState& state, ReSyne::RecorderState& recorderState) {
    if (ImGui::CollapsingHeader("Output Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10);

        bool isPlaybackActive = recorderState.audioOutput && recorderState.audioOutput->isPlaying();
        std::vector<float> magnitudes;
        std::vector<float> phases;
        float sampleRate;

		float storedLoudness = ColourMapper::LOUDNESS_DB_UNSPECIFIED;
	        if (isPlaybackActive && !recorderState.samples.empty()) {
            std::lock_guard<std::mutex> lock(recorderState.samplesMutex);
            const float position = std::clamp(recorderState.timeline.scrubberNormalisedPosition, 0.0f, 1.0f) *
                                   (static_cast<float>(recorderState.samples.size()) - 1.0f);
            const size_t sampleIndex = static_cast<size_t>(position);
            const size_t clampedIndex = std::min(sampleIndex, recorderState.samples.size() - 1);

            const auto& currentSample = recorderState.samples[clampedIndex];
            magnitudes = !currentSample.magnitudes.empty() ? currentSample.magnitudes[0] : std::vector<float>();
            phases = !currentSample.phases.empty() ? currentSample.phases[0] : std::vector<float>();
            sampleRate = currentSample.sampleRate;
			if (std::isfinite(currentSample.loudnessLUFS)) {
				storedLoudness = currentSample.loudnessLUFS;
			}
        } else {
            magnitudes = audioInput.getFFTProcessor().getMagnitudesBuffer();
            phases = audioInput.getFFTProcessor().getPhaseBuffer();
            sampleRate = audioInput.getSampleRate();
        }

		sanitiseMagnitudes(magnitudes);

		float loudnessOverride = storedLoudness;
		if (!isPlaybackActive) {
			loudnessOverride = audioInput.getFFTProcessor().getMomentaryLoudnessLUFS();
		}

		auto currentColourResult = ColourMapper::spectrumToColour(
			magnitudes,
			phases,
			{},
			sampleRate,
			UIConstants::DEFAULT_GAMMA,
			state.visualSettings.colourSpace,
			state.visualSettings.gamutMappingEnabled,
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
                                 float sidebarWidth,
                                 float sidebarPadding,
                                 float labelWidth,
                                 float controlWidth,
                                 float buttonHeight) {
    if (ImGui::CollapsingHeader("Visualiser Settings")) {
        ImGui::Indent(10);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Smoothing");
        ImGui::SameLine(sidebarPadding + labelWidth);
        ImGui::SetCursorPosX(sidebarWidth - sidebarPadding - controlWidth);
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::SliderFloat("##Smoothing", &smoothingAmount, 0.0f, 1.0f, "%.2f")) {
            colourSmoother.setSmoothingAmount(smoothingAmount);
        }

        ImGui::SetCursorPosX(sidebarPadding);
        if (ImGui::Button("Reset Smoothing", ImVec2(130, buttonHeight))) {
            smoothingAmount = UIConstants::DEFAULT_SMOOTHING_SPEED;
            colourSmoother.setSmoothingAmount(smoothingAmount);
        }

        ImGui::Unindent(10);
        ImGui::Spacing();
    }
}

void renderEQControlsPanel(float& lowGain,
                          float& midGain,
                          float& highGain,
                          bool& showSpectrumAnalyser,
                          float& spectrumSmoothingFactor,
                          float sidebarWidth,
                          float sidebarPadding,
                          float labelWidth,
                          float controlWidth,
                          float buttonHeight,
                          float contentWidth) {
    if (ImGui::CollapsingHeader("Gain Adjustment")) {
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

        float buttonWidth = (contentWidth - ImGui::GetStyle().ItemSpacing.x) / 2;
        ImGui::SetCursorPosX(sidebarPadding);
        if (ImGui::Button("Reset EQ", ImVec2(buttonWidth, buttonHeight))) {
            lowGain = midGain = highGain = 1.0f;
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(sidebarPadding + buttonWidth + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(showSpectrumAnalyser ? "Hide Spectrum" : "Show Spectrum",
                          ImVec2(buttonWidth, buttonHeight))) {
            showSpectrumAnalyser = !showSpectrumAnalyser;
        }

        if (showSpectrumAnalyser) {
            ImGui::Spacing();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Spectrum Smoothing");
            ImGui::SameLine(sidebarPadding + labelWidth);
            ImGui::SetCursorPosX(sidebarWidth - sidebarPadding - controlWidth);
            ImGui::SetNextItemWidth(controlWidth);
            ImGui::SliderFloat("##SpectrumSmoothing", &spectrumSmoothingFactor, 0.0f, 1.0f, "%.2f");
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
            bool configChanged = false;

            ImGui::Text("Destination");
            char destinationHost[] = "127.0.0.1";
            ImGui::BeginDisabled();
            ImGui::InputText("##OSCDestination", destinationHost, sizeof(destinationHost), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();

            ImGui::Text("Transmit Port");
            configChanged |= ImGui::InputInt("##OSCTransmitPort", &state.oscSettings.transmitPort);

            ImGui::Text("Receive Port");
            configChanged |= ImGui::InputInt("##OSCReceivePort", &state.oscSettings.receivePort);

            state.oscSettings.transmitPort = std::clamp(state.oscSettings.transmitPort, 1, 65535);
            state.oscSettings.receivePort = std::clamp(state.oscSettings.receivePort, 1, 65535);

            ImGui::Spacing();
            ImGui::Text("Transport Status: %s", osc.isRunning() ? "Running" : "Stopped");
            ImGui::Text("Frames Sent: %llu", static_cast<unsigned long long>(stats.framesSent));
            ImGui::Text("Messages Received: %llu", static_cast<unsigned long long>(stats.messagesReceived));
            
            if (osc.isRunning()) {
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
            bool transportRunning = osc.isRunning();

            if (configChanged && transportRunning) {
                Synesthesia::OSC::OSCConfig config;
                config.transmitPort = static_cast<uint16_t>(state.oscSettings.transmitPort);
                config.receivePort = static_cast<uint16_t>(state.oscSettings.receivePort);
                osc.updateConfig(config);
            }

            const float buttonWidth = ImGui::GetContentRegionAvail().x;
            if (ImGui::Button(transportRunning ? "Disable" : "Enable", ImVec2(buttonWidth, 0))) {
                if (transportRunning) {
                    state.oscEnabled = false;
                    osc.stop();
                } else {
                    state.oscEnabled = true;
                    Synesthesia::OSC::OSCConfig config;
                    config.transmitPort = static_cast<uint16_t>(state.oscSettings.transmitPort);
                    config.receivePort = static_cast<uint16_t>(state.oscSettings.receivePort);
                    osc.start(config);
                }
            }

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
