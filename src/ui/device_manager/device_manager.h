#pragma once

#include "audio_input.h"
#include "audio_output.h"
#include <vector>
#include <string>

struct DeviceState {
    int selectedDeviceIndex = -1;
    int selectedChannelIndex = 0;
    bool streamError = false;
    std::string streamErrorMessage;

    std::vector<const char*> deviceNames;
    std::vector<const char*> channelNames;
    std::vector<std::string> channelNameStrings;
    bool deviceNamesPopulated = false;

    int selectedOutputDeviceIndex = -1;
    std::vector<const char*> outputDeviceNames;
    bool outputDeviceNamesPopulated = false;
};

struct DeviceSelectionResult {
    bool success;
    std::string errorMessage;
};

class DeviceManager {
public:
    static void populateDeviceNames(DeviceState& deviceState,
                                   const std::vector<AudioInput::DeviceInfo>& devices);

    static void populateOutputDeviceNames(DeviceState& deviceState,
                                         const std::vector<AudioOutput::DeviceInfo>& outputDevices);

    static bool selectDevice(DeviceState& deviceState,
                            AudioInput& audioInput,
                            const std::vector<AudioInput::DeviceInfo>& devices,
                            int newDeviceIndex);

    static void selectChannel(DeviceState& deviceState,
                             AudioInput& audioInput,
                             int newChannelIndex);

    static void renderDeviceSelection(DeviceState& deviceState,
                                     AudioInput& audioInput,
                                     const std::vector<AudioInput::DeviceInfo>& devices);

    static void renderOutputDeviceSelection(DeviceState& deviceState,
                                           const std::vector<AudioOutput::DeviceInfo>& outputDevices);

    static void renderChannelSelection(DeviceState& deviceState,
                                      AudioInput& audioInput,
                                      const std::vector<AudioInput::DeviceInfo>& devices);

private:
    static void createChannelNames(DeviceState& deviceState, int channelsToUse);
    static void resetDeviceState(DeviceState& deviceState);
    static DeviceSelectionResult validateAndSelectDevice(DeviceState& deviceState,
                                                        AudioInput& audioInput,
                                                        const std::vector<AudioInput::DeviceInfo>& devices,
                                                        int newDeviceIndex);
};
