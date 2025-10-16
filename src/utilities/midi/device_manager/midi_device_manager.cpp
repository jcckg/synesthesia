#include "midi_device_manager.h"
#include <imgui.h>

void MIDIDeviceManager::populateMIDIDeviceNames(MIDIDeviceState& deviceState,
												const std::vector<MIDIInput::DeviceInfo>& devices) {
	if (!deviceState.deviceNamesPopulated && !devices.empty()) {
		deviceState.deviceNames.reserve(devices.size());
		for (const auto& dev : devices) {
			deviceState.deviceNames.push_back(dev.name.c_str());
		}
		deviceState.deviceNamesPopulated = true;
	}
}

bool MIDIDeviceManager::selectMIDIDevice(MIDIDeviceState& deviceState,
										 MIDIInput& midiInput,
										 const std::vector<MIDIInput::DeviceInfo>& devices,
										 int newDeviceIndex) {
	MIDIDeviceSelectionResult result = validateAndSelectDevice(deviceState, midiInput, devices, newDeviceIndex);

	if (result.success) {
		deviceState.connectionError = false;
		deviceState.errorMessage.clear();
	} else {
		deviceState.connectionError = true;
		deviceState.errorMessage = result.errorMessage;
		if (newDeviceIndex < 0 || static_cast<size_t>(newDeviceIndex) >= devices.size()) {
			deviceState.selectedDeviceIndex = -1;
		}
	}

	return result.success;
}

void MIDIDeviceManager::renderMIDIDeviceSelection(MIDIDeviceState& deviceState,
												  MIDIInput& midiInput,
												  const std::vector<MIDIInput::DeviceInfo>& devices) {
	ImGui::Text("MIDI DEVICE");
	ImGui::SetNextItemWidth(-FLT_MIN);

	if (!deviceState.deviceNames.empty()) {
		if (ImGui::Combo("##mididevice", &deviceState.selectedDeviceIndex,
						 deviceState.deviceNames.data(),
						 static_cast<int>(deviceState.deviceNames.size()))) {
			selectMIDIDevice(deviceState, midiInput, devices, deviceState.selectedDeviceIndex);
		}

		if (deviceState.connectionError) {
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
							   deviceState.errorMessage.c_str());
		} else if (deviceState.selectedDeviceIndex < 0) {
			ImGui::TextDisabled("Select a MIDI device");
		}
	} else {
		ImGui::TextDisabled("No MIDI input devices found.");
	}
	ImGui::Spacing();
}

bool MIDIDeviceManager::autoDetectAndConnect(MIDIDeviceState& deviceState,
											 MIDIInput& midiInput,
											 const std::vector<MIDIInput::DeviceInfo>& devices) {
	if (devices.empty()) {
		return false;
	}

	return selectMIDIDevice(deviceState, midiInput, devices, 0);
}

bool MIDIDeviceManager::checkForNewMIDIDevices(MIDIDeviceState& deviceState,
											   MIDIInput& midiInput,
											   std::vector<MIDIInput::DeviceInfo>& devices,
											   bool& devicesChanged) {
	std::vector<MIDIInput::DeviceInfo> currentDevices = MIDIInput::getMIDIInputDevices();

	if (currentDevices.size() != devices.size()) {
		devicesChanged = true;
		devices = currentDevices;

		deviceState.deviceNames.clear();
		deviceState.deviceNamesPopulated = false;
		populateMIDIDeviceNames(deviceState, devices);

		if (!devices.empty() && !midiInput.isMIDIInputActive()) {
			return autoDetectAndConnect(deviceState, midiInput, devices);
		}

		if (devices.empty() && midiInput.isMIDIInputActive()) {
			midiInput.closeMIDIInput();
			resetMIDIDeviceState(deviceState);
			return false;
		}
	}

	devicesChanged = false;
	return midiInput.isMIDIInputActive();
}

void MIDIDeviceManager::resetMIDIDeviceState(MIDIDeviceState& deviceState) {
	deviceState.selectedDeviceIndex = -1;
	deviceState.connectionError = false;
	deviceState.errorMessage.clear();
}

MIDIDeviceSelectionResult MIDIDeviceManager::validateAndSelectDevice(MIDIDeviceState& deviceState,
																	  MIDIInput& midiInput,
																	  const std::vector<MIDIInput::DeviceInfo>& devices,
																	  int newDeviceIndex) {
	MIDIDeviceSelectionResult result;
	result.success = false;

	if (newDeviceIndex < 0 || static_cast<size_t>(newDeviceIndex) >= devices.size()) {
		result.errorMessage = "Invalid device index";
		return result;
	}

	bool success = midiInput.initMIDIInput(newDeviceIndex);
	if (success) {
		deviceState.selectedDeviceIndex = newDeviceIndex;
		result.success = true;
	} else {
		result.errorMessage = "Failed to open MIDI device";
	}

	return result;
}
