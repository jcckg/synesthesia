#include "controls.h"

#include <imgui.h>
#include "imgui_internal.h"
#include "ui/styling/styling.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <vector>

#include "audio/analysis/presentation/sample_sequence.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "ui.h"
#include "resyne/recorder/recorder.h"
#include "resyne/recorder/presentation_worker.h"
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

const char* displaySurfacePrecisionLabel(const PresentationDiagnostics::DisplaySurfacePrecision precision) {
    switch (precision) {
        case PresentationDiagnostics::DisplaySurfacePrecision::TenBit:
            return "10-bit";
        case PresentationDiagnostics::DisplaySurfacePrecision::EightBit:
            return "8-bit fallback";
        case PresentationDiagnostics::DisplaySurfacePrecision::Unknown:
        default:
            return "unknown";
    }
}

const char* texturePrecisionLabel(const PresentationDiagnostics& diagnostics) {
    if (!diagnostics.presentationResourcesAvailable) {
        return "standard widgets";
    }

    return diagnostics.highPrecisionTexturesAvailable
        ? "high precision"
        : "8-bit fallback";
}

const char* backgroundPresentationLabel(const PresentationDiagnostics& diagnostics) {
    if (!diagnostics.presentationResourcesAvailable) {
        return "standard";
    }

    return diagnostics.backgroundPresentationAvailable
        ? "texture-backed"
        : "clear-colour fallback";
}

}

namespace Controls {

bool renderSectionHeader(const char* label, ImGuiTreeNodeFlags flags, SectionLayer layer) {
    if (layer == SectionLayer::Secondary) {
        const float shadeOffset = ImGui::GetStyleColorVec4(ImGuiCol_Text).x > 0.5f ? -0.020f : 0.035f;
        for (ImGuiCol role : {ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive}) {
            ImVec4 colour = ImGui::GetStyleColorVec4(role);
            colour.x = std::clamp(colour.x + shadeOffset, 0.0f, 1.0f);
            colour.y = std::clamp(colour.y + shadeOffset, 0.0f, 1.0f);
            colour.z = std::clamp(colour.z + shadeOffset, 0.0f, 1.0f);
            ImGui::PushStyleColor(role, colour);
        }
    }
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
    ImGui::GetWindowDrawList()->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y),
        ImGui::GetColorU32(ImGuiCol_Header), ImGui::GetStyle().FrameRounding);
    const bool open = ImGui::TreeNodeEx(label, flags | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth |
        ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_NoAutoOpenOnLog);
    if (layer == SectionLayer::Secondary) ImGui::PopStyleColor(3);
    return open;
}

bool renderSidebarSectionHeader(const char* label, ImGuiTreeNodeFlags flags, SectionLayer layer) {
    constexpr float inset = 6.0f;
    auto* window = ImGui::GetCurrentWindow();
    const float cursorX = window->DC.CursorPos.x;
    const float contentRight = window->ContentRegionRect.Max.x;
    const float workRight = window->WorkRect.Max.x;
    window->ContentRegionRect.Max.x = window->Pos.x + window->Size.x - inset;
    window->WorkRect.Max.x = window->ContentRegionRect.Max.x;
    ImGui::SetCursorPosX(inset);
    const bool open = renderSectionHeader(label, flags, layer);
    window->ContentRegionRect.Max.x = contentRight;
    window->WorkRect.Max.x = workRight;
    window->DC.CursorPos.x = cursorX;
    return open;
}

void renderFrequencyInfoPanel(AudioInput& audioInput, ReSyne::RecorderState& recorderState) {
    if (!renderSidebarSectionHeader("Audio and output", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ColourCore::FrameResult result{};
    const bool playback = recorderState.audioOutput && recorderState.audioOutput->getTotalFrames() > 0;
    if (playback) {
        const auto presentation = ReSyne::recorderPresentation(recorderState);
        if (presentation && presentation->analysis && !presentation->analysis->empty()) {
            const double time = recorderState.metadata.sampleRate > 0.0f ?
                static_cast<double>(recorderState.audioOutput->getPresentationPosition()) / static_cast<double>(recorderState.metadata.sampleRate) :
                static_cast<double>(recorderState.timeline.scrubberNormalisedPosition) * presentation->duration;
            result = (*presentation->analysis)[presentation->analysis->indexAt(time)].analysis;
        }
    } else if (const auto live = audioInput.getAudioProcessor().getPresentation()) result = live->frame.analysis;
    if (result.dominantFrequency > 0.0f) {
        ImGui::Text("Spectral centroid: %.1f Hz", static_cast<double>(result.spectralCentroid));
        ImGui::Text("Peak frequency: %.1f Hz", static_cast<double>(result.dominantFrequency));
        if (std::isfinite(result.loudnessDb)) ImGui::Text(result.loudnessIsLufs ? "Loudness: %.1f LUFS" : "Frame level: %.1f dBFS",
            static_cast<double>(result.loudnessDb));
        else ImGui::TextDisabled("Loudness: unavailable");
    } else ImGui::TextDisabled("No significant frequencies");
    ImGui::Spacing();
}

void renderColourSmoothingPanel(float& smoothingAmount, bool& manualSmoothing,
                                 bool& showSpectrumAnalyser, float sidebarWidth,
                                 float sidebarPadding, float buttonHeight,
                                 ReSyne::RecorderState* recorder, float effectiveResponseSeconds, ColourPresentation::ResponseRange responseRange) {
    if (!renderSidebarSectionHeader("Colour Smoothing")) return;
    const float contentWidth = std::max(40.0f, sidebarWidth - sidebarPadding * 2.0f);
    int mode = manualSmoothing ? 1 : 0;
    ImGui::SetNextItemWidth(contentWidth);
    if (UIStyler::combo("##ColourResponseMode", &mode, "Automatic\0Manual\0")) manualSmoothing = mode == 1;
    if (manualSmoothing) {
        float responseMs = manualResponseTime(smoothingAmount) * 1000.0f;
        ImGui::TextUnformatted("Base response time");
        ImGui::SetNextItemWidth(contentWidth);
        if (ImGui::SliderFloat("##ColourResponseTime", &responseMs, 40.0f, 1500.0f, "%.0f ms", ImGuiSliderFlags_Logarithmic)) {
            smoothingAmount = manualSmoothingAmount(responseMs / 1000.0f);
        }
    } else {
        ImGui::TextWrapped("Local audio-driven response");
    }
    float effectiveMs = effectiveResponseSeconds * 1000.0f;
    const auto presentation = recorder ? ReSyne::recorderPresentation(*recorder) : nullptr;
    if (!manualSmoothing && presentation && presentation->colours->size() > 0 &&
        (recorder->isRecording || (recorder->audioOutput && recorder->audioOutput->getTotalFrames() > 0))) {
        const double time = recorder->isRecording ? presentation->colours->endTime()
            : recorder->audioOutput && recorder->metadata.sampleRate > 0.0f
                ? recorder->audioOutput->getPresentationPosition() / static_cast<double>(recorder->metadata.sampleRate)
                : static_cast<double>(recorder->timeline.scrubberNormalisedPosition) * presentation->duration;
        const auto point = presentation->colours->pointAt(time);
        effectiveMs = point.responseSeconds * 1000.0f;
        if (presentation->analysis) responseRange = ColourPresentation::responseRange(presentation->analysis->contributionsAt(time));
        if (!presentation->message.empty()) ImGui::TextWrapped("%s", presentation->message.c_str());
        if (presentation->message.empty() && presentation->settings.manualSmoothing != manualSmoothing)
            ImGui::TextDisabled("Updating colour response...");
    }
    if (!manualSmoothing) {
        if (recorder && !recorder->presentationSmoothingEnabled) ImGui::TextDisabled("Smoothing off");
        else if (responseRange.maximum > 0.0f)
            ImGui::Text("Spectral integration: %.0f–%.0f ms", static_cast<double>(responseRange.minimum * 1000.0f),
                static_cast<double>(responseRange.maximum * 1000.0f));
        else if (effectiveMs > 0.0f) ImGui::Text("Spectral integration: %.0f ms", static_cast<double>(effectiveMs));
        else ImGui::TextDisabled("Stored response");
    }
    if (manualSmoothing && ImGui::Button("Reset response", {contentWidth, buttonHeight})) {
        smoothingAmount = UIConstants::DEFAULT_SMOOTHING_SPEED;
    }
    if (ImGui::Button(showSpectrumAnalyser ? "Hide spectrum" : "Show spectrum", {contentWidth, buttonHeight}))
        showSpectrumAnalyser = !showSpectrumAnalyser;
    ImGui::Spacing();
}

void renderEQControlsPanel(float& lowGain,
                          float& midGain,
                          float& highGain,
                          const bool enabled,
                          float sidebarWidth,
                          float sidebarPadding,
                          float labelWidth,
                          float controlWidth,
                          float buttonHeight,
                          float contentWidth) {
    if (renderSidebarSectionHeader("EQ/Gain")) {

        if (!enabled) {
            ImGui::TextDisabled("Live input only");
            ImGui::Spacing();
            ImGui::BeginDisabled();
        }

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

        if (!enabled) {
            ImGui::EndDisabled();
        }

    }
}

void renderAdvancedSettingsPanel(UIState& state
#ifdef ENABLE_MIDI
                                  , MIDIInput* midiInput
                                  , const std::vector<MIDIInput::DeviceInfo>* midiDevices
#endif
                                  ) {
    if (renderSidebarSectionHeader("Advanced Settings")) {
        if (renderSidebarSectionHeader("Appearance", 0, SectionLayer::Secondary)) {
            ImGui::Text("Sidebar: %s", state.visibility.sidebarOnLeft ? "Left" : "Right");
            if (ImGui::Button("Swap Sides")) {
                state.visibility.sidebarOnLeft = !state.visibility.sidebarOnLeft;
            }

            ImGui::Spacing();
            bool currentSmoothingState = state.visualSettings.smoothingEnabled;
            if (ImGui::Checkbox("Enable Smoothing", &currentSmoothingState)) {
                if (!currentSmoothingState) {
                    ImGui::OpenPopup("Photosensitivity Warning");
                } else {
                    state.visualSettings.smoothingEnabled = true;
                }
            }

            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(600.0f, std::max(1.0f, viewport->WorkSize.x - 24.0f)), 0.0f));
            if (ImGui::BeginPopupModal("Photosensitivity Warning", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
                ImGui::TextWrapped("Warning: Disabling smoothing will cause rapidly flashing colours which can trigger photosensitive epilepsy in sensitive individuals.");
                ImGui::Spacing();
                ImGui::TextWrapped("Are you sure you want to disable smoothing?");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();

                const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if (ImGui::Button("(Yes) Disable Smoothing", ImVec2(buttonWidth, 0.0f))) {
                    state.visualSettings.smoothingEnabled = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("(No) Keep Smoothing Enabled", ImVec2(buttonWidth, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            int colourSpaceIndex = 0;
            switch (state.visualSettings.colourSpace) {
                case ColourCore::ColourSpace::Rec2020:
                    colourSpaceIndex = 0;
                    break;
                case ColourCore::ColourSpace::DisplayP3:
                    colourSpaceIndex = 1;
                    break;
                case ColourCore::ColourSpace::SRGB:
                    colourSpaceIndex = 2;
                    break;
            }
            const char* colourSpaceLabels[] = {"Rec.2020", "Display P3", "sRGB"};
            ImGui::Text("Working Colour Space");
            if (UIStyler::combo("##WorkingColourSpace", &colourSpaceIndex, colourSpaceLabels, IM_ARRAYSIZE(colourSpaceLabels))) {
                switch (colourSpaceIndex) {
                    case 0:
                        state.visualSettings.colourSpace = ColourCore::ColourSpace::Rec2020;
                        break;
                    case 1:
                        state.visualSettings.colourSpace = ColourCore::ColourSpace::DisplayP3;
                        break;
                    case 2:
                    default:
                        state.visualSettings.colourSpace = ColourCore::ColourSpace::SRGB;
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

            ImGui::Spacing();
            ImGui::TextDisabled("Presentation Debug");
            std::array<char, 192> debugText{};
            std::snprintf(
                debugText.data(),
                debugText.size(),
                "Textures: %s\nDisplay: %s\nBackground: %s",
                texturePrecisionLabel(state.presentationDiagnostics),
                displaySurfacePrecisionLabel(state.presentationDiagnostics.displaySurfacePrecision),
                backgroundPresentationLabel(state.presentationDiagnostics));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            renderWrappedStatusText(debugText.data());
            ImGui::PopStyleColor();

        }

#ifdef ENABLE_OSC
        if (renderSidebarSectionHeader("OSC", 0, SectionLayer::Secondary)) {
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

        }
#endif

#ifdef ENABLE_MIDI
        if (midiInput && midiDevices) {
            if (renderSidebarSectionHeader("MIDI", 0, SectionLayer::Secondary)) {
                ImGui::Text("MIDI INPUT DEVICE");
                MIDIDeviceManager::renderMIDIDeviceSelection(state.midiDeviceState, *midiInput, *midiDevices);

            }
        }
#endif

    }
}

}
