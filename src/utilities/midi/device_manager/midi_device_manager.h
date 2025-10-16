#pragma once

#include "midi_input.h"
#include <vector>
#include <string>

struct MIDIDeviceState {
	int selectedDeviceIndex = -1;
	bool connectionError = false;
	std::string errorMessage;

	std::vector<const char*> deviceNames;
	bool deviceNamesPopulated = false;
};

struct MIDIDeviceSelectionResult {
	bool success;
	std::string errorMessage;
};

class MIDIDeviceManager {
public:
	static void populateMIDIDeviceNames(MIDIDeviceState& deviceState,
										const std::vector<MIDIInput::DeviceInfo>& devices);

	static bool selectMIDIDevice(MIDIDeviceState& deviceState,
								 MIDIInput& midiInput,
								 const std::vector<MIDIInput::DeviceInfo>& devices,
								 int newDeviceIndex);

	static bool autoDetectAndConnect(MIDIDeviceState& deviceState,
									 MIDIInput& midiInput,
									 const std::vector<MIDIInput::DeviceInfo>& devices);

	static bool checkForNewMIDIDevices(MIDIDeviceState& deviceState,
									   MIDIInput& midiInput,
									   std::vector<MIDIInput::DeviceInfo>& devices,
									   bool& devicesChanged);

	static void renderMIDIDeviceSelection(MIDIDeviceState& deviceState,
										  MIDIInput& midiInput,
										  const std::vector<MIDIInput::DeviceInfo>& devices);

private:
	static void resetMIDIDeviceState(MIDIDeviceState& deviceState);
	static MIDIDeviceSelectionResult validateAndSelectDevice(MIDIDeviceState& deviceState,
															  MIDIInput& midiInput,
															  const std::vector<MIDIInput::DeviceInfo>& devices,
															  int newDeviceIndex);
};
