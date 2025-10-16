#include "midi_handler.h"

#ifdef ENABLE_MIDI

namespace UIHandlers {

void MIDIHandler::update(UIState& state, const float deltaTime, MIDIInput* midiInput,
                         std::vector<MIDIInput::DeviceInfo>* midiDevices) {
	if (!midiInput || !midiDevices) {
		return;
	}

	state.lastMIDIDeviceCheckTime += deltaTime;
	if (state.lastMIDIDeviceCheckTime >= 2.0f) {
		state.lastMIDIDeviceCheckTime = 0.0f;

		bool devicesChanged = false;
		MIDIDeviceManager::checkForNewMIDIDevices(
			state.midiDeviceState, *midiInput, *midiDevices, devicesChanged);

		state.midiDevicesAvailable = !midiDevices->empty();
	}
}

}

#endif
