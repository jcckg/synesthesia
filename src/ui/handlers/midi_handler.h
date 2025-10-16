#pragma once

#include "ui.h"

#ifdef ENABLE_MIDI
#include "midi_input.h"
#include "device_manager.h"

namespace UIHandlers {

class MIDIHandler {
public:
	static void update(UIState& state, float deltaTime, MIDIInput* midiInput,
	                   std::vector<MIDIInput::DeviceInfo>* midiDevices);
};

}
#endif
