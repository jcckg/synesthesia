#include "device_manager.h"
#include "device_selector.h"
#include <imgui.h>
#include <portaudio.h>
#include <algorithm>
#include <array>
#include <string>

namespace {

std::vector<DeviceSelector::Item> buildSelectorItems(const std::vector<const char*>& names,
                                                     const int selectedIndex,
                                                     const bool selectedActive,
                                                     const std::array<float, 2>& selectedLevels) {
    std::vector<DeviceSelector::Item> items;
    items.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        const bool useLevels = selectedActive && selectedIndex == static_cast<int>(i);
        items.push_back(DeviceSelector::Item{
            names[i],
            useLevels ? selectedLevels[0] : 0.0f,
            useLevels ? selectedLevels[1] : 0.0f
        });
    }
    return items;
}

std::vector<DeviceSelector::Item> buildInputSelectorItems(
    const std::vector<const char*>& names,
    const std::vector<AudioInput::DeviceInfo>& devices,
    const int selectedIndex,
    const bool selectedActive,
    const std::array<float, 2>& selectedLevels,
    const AudioInputLevelMonitor& inputLevelMonitor) {
    std::vector<DeviceSelector::Item> items;
    items.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        std::array<float, 2> levels = inputLevelMonitor.getStereoLevels(i);
        if (selectedActive && selectedIndex == static_cast<int>(i)) {
            levels = selectedLevels;
        }

        const char* label = names[i];
        if (i < devices.size()) {
            label = devices[i].name.c_str();
        }

        items.push_back(DeviceSelector::Item{label, levels[0], levels[1]});
    }

    return items;
}

}

void DeviceManager::populateDeviceNames(DeviceState& deviceState,
                                        const std::vector<AudioInput::DeviceInfo>& devices) {
    if (!deviceState.deviceNamesPopulated && !devices.empty()) {
        deviceState.deviceNames.reserve(devices.size());
        for (const auto& dev : devices) {
            deviceState.deviceNames.push_back(dev.name.c_str());
        }
        deviceState.deviceNamesPopulated = true;
    }
}

void DeviceManager::populateOutputDeviceNames(DeviceState& deviceState,
                                              const std::vector<AudioOutput::DeviceInfo>& outputDevices) {
    if (!deviceState.outputDeviceNamesPopulated && !outputDevices.empty()) {
        deviceState.outputDeviceNames.reserve(outputDevices.size());
        for (const auto& dev : outputDevices) {
            deviceState.outputDeviceNames.push_back(dev.name.c_str());
        }
        deviceState.outputDeviceNamesPopulated = true;
        if (deviceState.selectedOutputDeviceIndex < 0) {
            PaDeviceIndex defaultDevice = Pa_GetDefaultOutputDevice();
            if (defaultDevice != paNoDevice) {
                for (size_t i = 0; i < outputDevices.size(); ++i) {
                    if (outputDevices[i].paIndex == defaultDevice) {
                        deviceState.selectedOutputDeviceIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
    }
}

bool DeviceManager::selectDevice(DeviceState& deviceState, 
                                AudioInput& audioInput,
                                const std::vector<AudioInput::DeviceInfo>& devices,
                                int newDeviceIndex) {
    DeviceSelectionResult result = validateAndSelectDevice(deviceState, audioInput, devices, newDeviceIndex);
    
    if (result.success) {
        deviceState.streamError = false;
        deviceState.streamErrorMessage.clear();
    } else {
        deviceState.streamError = true;
        deviceState.streamErrorMessage = result.errorMessage;
        if (newDeviceIndex < 0 || static_cast<size_t>(newDeviceIndex) >= devices.size()) {
            deviceState.selectedDeviceIndex = -1;
        }
    }
    
    return result.success;
}

void DeviceManager::selectChannel(DeviceState& deviceState, 
                                 AudioInput& audioInput,
                                 int newChannelIndex) {
    deviceState.selectedChannelIndex = newChannelIndex;
    audioInput.setActiveChannel(newChannelIndex);
}

void DeviceManager::renderDeviceSelection(DeviceState& deviceState,
                                         AudioInput& audioInput,
                                         const std::vector<AudioInput::DeviceInfo>& devices,
                                         const AudioInputLevelMonitor& inputLevelMonitor) {
    ImGui::Text("INPUT DEVICE");
    
    if (!deviceState.deviceNames.empty()) {
        const bool selectedActive =
            deviceState.selectedDeviceIndex >= 0 &&
            !deviceState.streamError &&
            audioInput.isStreamActive();
        const std::vector<DeviceSelector::Item> items = buildInputSelectorItems(
            deviceState.deviceNames,
            devices,
            deviceState.selectedDeviceIndex,
            selectedActive,
            audioInput.getStereoLevels(),
            inputLevelMonitor);
        if (DeviceSelector::renderCombo("##device", deviceState.selectedDeviceIndex, items)) {
            selectDevice(deviceState, audioInput, devices, deviceState.selectedDeviceIndex);
        }
        
        if (deviceState.streamError) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                              deviceState.streamErrorMessage.c_str());
        } else if (deviceState.selectedDeviceIndex < 0) {
            ImGui::TextDisabled("Select an audio device");
        }
    } else {
        ImGui::TextDisabled("No audio input devices found.");
    }
    ImGui::Spacing();
}

void DeviceManager::renderOutputDeviceSelection(DeviceState& deviceState,
                                               const std::vector<AudioOutput::DeviceInfo>&,
                                               const bool outputDeviceActive,
                                               const std::array<float, 2>& outputLevels) {
    ImGui::Text("OUTPUT DEVICE");

    if (!deviceState.outputDeviceNames.empty()) {
        const std::vector<DeviceSelector::Item> items = buildSelectorItems(
            deviceState.outputDeviceNames,
            deviceState.selectedOutputDeviceIndex,
            outputDeviceActive && deviceState.selectedOutputDeviceIndex >= 0,
            outputLevels);
        DeviceSelector::renderCombo("##outputdevice", deviceState.selectedOutputDeviceIndex, items);

        if (deviceState.selectedOutputDeviceIndex < 0) {
            ImGui::TextDisabled("Select an audio output device");
        }
    } else {
        ImGui::TextDisabled("No audio output devices found.");
    }
    ImGui::Spacing();
}

void DeviceManager::renderChannelSelection(DeviceState& deviceState,
                                          AudioInput& audioInput,
                                          const std::vector<AudioInput::DeviceInfo>& devices) {
    if (deviceState.selectedDeviceIndex >= 0 &&
        !deviceState.streamError &&
        !deviceState.channelNames.empty() &&
        devices[static_cast<size_t>(deviceState.selectedDeviceIndex)].maxChannels > 2) {

        ImGui::Text("CHANNEL");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##channel", &deviceState.selectedChannelIndex,
                        deviceState.channelNames.data(),
                        static_cast<int>(deviceState.channelNames.size()))) {
            selectChannel(deviceState, audioInput, deviceState.selectedChannelIndex);
        }
        ImGui::Spacing();
    }
}

void DeviceManager::createChannelNames(DeviceState& deviceState, int channelsToUse) {
    deviceState.channelNameStrings.clear();
    deviceState.channelNameStrings.reserve(static_cast<size_t>(channelsToUse));
    deviceState.channelNames.clear();
    deviceState.channelNames.reserve(static_cast<size_t>(channelsToUse));
    
    for (int i = 0; i < channelsToUse; i++) {
        deviceState.channelNameStrings.push_back("Channel " + std::to_string(i + 1));
        deviceState.channelNames.push_back(deviceState.channelNameStrings.back().c_str());
    }
}

void DeviceManager::resetDeviceState(DeviceState& deviceState) {
    deviceState.selectedDeviceIndex = -1;
    deviceState.selectedChannelIndex = 0;
    deviceState.streamError = false;
    deviceState.streamErrorMessage.clear();
    deviceState.deviceNames.clear();
    deviceState.channelNames.clear();
    deviceState.channelNameStrings.clear();
    deviceState.deviceNamesPopulated = false;
}

DeviceSelectionResult DeviceManager::validateAndSelectDevice(DeviceState& deviceState,
                                                           AudioInput& audioInput,
                                                           const std::vector<AudioInput::DeviceInfo>& devices,
                                                           int newDeviceIndex) {
    if (newDeviceIndex < 0 || static_cast<size_t>(newDeviceIndex) >= devices.size()) {
        return {false, "Invalid device selection index."};
    }
    
    deviceState.channelNames.clear();
    int maxChannels = devices[static_cast<size_t>(newDeviceIndex)].maxChannels;
    deviceState.selectedChannelIndex = 0;
    int channelsToUse = std::min(maxChannels, 16);

    if (!audioInput.initStream(devices[static_cast<size_t>(newDeviceIndex)].paIndex, channelsToUse)) {
        return {false, "Error opening device!"};
    }
    
    deviceState.selectedDeviceIndex = newDeviceIndex;
    createChannelNames(deviceState, channelsToUse);
    
    return {true, ""};
}
