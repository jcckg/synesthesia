#include "controls.h"

#include <imgui.h>
#include <vector>
#include <algorithm>
#include <mutex>

#include "colour_mapper.h"
#include "ui.h"
#include "resyne/recorder/recorder.h"
#ifdef ENABLE_API_SERVER
#include "synesthesia_api_integration.h"
#endif
#ifdef ENABLE_MIDI
#include "midi_device_manager.h"
#endif

namespace Controls {

void renderFrequencyInfoPanel(AudioInput& audioInput, float* clear_colour, const UIState& state, ReSyne::RecorderState& recorderState) {
    if (ImGui::CollapsingHeader("Output Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(10);

        bool isPlaybackActive = recorderState.audioOutput && recorderState.audioOutput->isPlaying();
        std::vector<float> magnitudes;
        std::vector<float> phases;
        float sampleRate;

        if (isPlaybackActive && !recorderState.samples.empty()) {
            std::lock_guard<std::mutex> lock(recorderState.samplesMutex);
            const float position = std::clamp(recorderState.timeline.scrubberNormalisedPosition, 0.0f, 1.0f) *
                                   (static_cast<float>(recorderState.samples.size()) - 1.0f);
            const size_t sampleIndex = static_cast<size_t>(position);
            const size_t clampedIndex = std::min(sampleIndex, recorderState.samples.size() - 1);

            const auto& currentSample = recorderState.samples[clampedIndex];
            magnitudes = currentSample.magnitudes;
            phases = currentSample.phases;
            sampleRate = currentSample.sampleRate;
        } else {
            magnitudes = audioInput.getFFTProcessor().getMagnitudesBuffer();
            phases = audioInput.getFFTProcessor().getPhaseBuffer();
            sampleRate = audioInput.getSampleRate();
        }

        auto currentColourResult = ColourMapper::spectrumToColour(
            magnitudes,
            phases,
            sampleRate,
            UIConstants::DEFAULT_GAMMA,
            state.visualSettings.colourSpace,
            state.visualSettings.gamutMappingEnabled);

        if (currentColourResult.dominantFrequency > 0.0f) {
            ImGui::Text("Spectral centroid: %.1f Hz", static_cast<double>(currentColourResult.dominantFrequency));
            ImGui::Text("Wavelength: %.1f nm", static_cast<double>(currentColourResult.dominantWavelength));
            ImGui::Text("RGB: (%.2f, %.2f, %.2f)", static_cast<double>(clear_colour[0]), static_cast<double>(clear_colour[1]), static_cast<double>(clear_colour[2]));
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

void renderAdvancedSettingsPanel(UIState& state, float contentWidth
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
        
#ifdef ENABLE_API_SERVER
        if (ImGui::CollapsingHeader("API Settings")) {
			ImGui::Indent(10);
            auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();

            ImGui::Text("Server Status: %s", api.isServerRunning() ? "Running" : "Stopped");
            
            auto clients = api.getConnectedClients();
            ImGui::Text("Connected Clients: %zu", clients.size());
            
            if (!clients.empty()) {
                ImGui::Indent();
                for (size_t i = 0; i < clients.size() && i < 5; ++i) {
                    const auto& clientName = clients[i];
                    if (clientName.length() > 25) {
                        const std::string truncated = clientName.substr(0, 22) + "...";
                        ImGui::Text("• %s", truncated.c_str());
                    } else {
                        ImGui::Text("• %s", clientName.c_str());
                    }
                }
                if (clients.size() > 5) {
                    ImGui::Text("... and %zu more", clients.size() - 5);
                }
                ImGui::Unindent();
            }
            
            ImGui::Text("Data Points: %zu", api.getLastDataSize());
            
            if (api.isServerRunning()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Performance");
                ImGui::Spacing();
                
                uint32_t current_fps = api.getCurrentFPS();
                bool high_perf = api.isHighPerformanceMode();
                float avg_frame_time = api.getAverageFrameTime();
                
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);

                ImGui::Text("FPS: %u", current_fps);
                ImGui::Text("Mode: %s", high_perf ? "High Perf" : "Standard");
                if (avg_frame_time > 0) {
                    ImGui::Text("Frame Time: %.2fms", static_cast<double>(avg_frame_time));
                    float estimated_latency = avg_frame_time;
                    ImGui::Text("Latency: ~%.1fms", static_cast<double>(estimated_latency));
                    
                    if (estimated_latency < 5.0f) {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Ultra-Low");
                    } else if (estimated_latency < 10.0f) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.0f, 1.0f), "✓ Low");
                    } else if (estimated_latency < 20.0f) {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "⚠ Moderate");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "⚠ High");
                    }
                }
                
                ImGui::Text("Total Frames: %llu", api.getTotalFramesSent());
                
                ImGui::PopTextWrapPos();
                ImGui::Separator();
            }
            
            ImGui::Spacing();
            bool serverRunning = api.isServerRunning();
            
            float buttonWidth = (contentWidth - ImGui::GetStyle().ItemSpacing.x) / 2;
            ImGui::PushItemWidth(buttonWidth);
            
            if (!serverRunning) {
                if (ImGui::Button("Enable", ImVec2(buttonWidth, 0))) {
                    state.apiServerEnabled = true;
                    api.startServer();
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.6f, 0.6f, 0.4f));
                ImGui::Button("Enable", ImVec2(buttonWidth, 0));
                ImGui::PopStyleColor();
            }
            
            ImGui::SameLine();
            
            if (serverRunning) {
                if (ImGui::Button("Disable", ImVec2(buttonWidth, 0))) {
                    state.apiServerEnabled = false;
                    api.stopServer();
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.6f, 0.6f, 0.4f));
                ImGui::Button("Disable", ImVec2(buttonWidth, 0));
                ImGui::PopStyleColor();
            }

			ImGui::PopItemWidth();

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
