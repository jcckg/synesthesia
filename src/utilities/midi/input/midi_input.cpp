#include "midi_input.h"
#include <iostream>

namespace {
    // MIDI protocol constants (status byte upper nibble)
    constexpr unsigned char MIDI_NOTE_OFF = 0x80;
    constexpr unsigned char MIDI_NOTE_ON = 0x90;
    constexpr unsigned char MIDI_AFTERTOUCH = 0xA0;
    constexpr unsigned char MIDI_CONTROL_CHANGE = 0xB0;
    constexpr unsigned char MIDI_PROGRAM_CHANGE = 0xC0;
    constexpr unsigned char MIDI_CHANNEL_PRESSURE = 0xD0;
    constexpr unsigned char MIDI_PITCH_BEND = 0xE0;
    constexpr unsigned char MIDI_STATUS_MASK = 0xF0;
    constexpr unsigned char MIDI_CHANNEL_MASK = 0x0F;
}

MIDIInput::MIDIInput() : isActive(false) {
	try {
		midiIn = std::make_unique<RtMidiIn>();
	} catch (RtMidiError& error) {
		std::cerr << "[MIDI] Error creating MIDI input: " << error.getMessage() << std::endl;
	}
}

MIDIInput::~MIDIInput() {
	closeMIDIInput();
}

std::vector<MIDIInput::DeviceInfo> MIDIInput::getMIDIInputDevices() {
	std::vector<DeviceInfo> devices;

	try {
		RtMidiIn midiIn;
		unsigned int portCount = midiIn.getPortCount();

		for (unsigned int i = 0; i < portCount; ++i) {
			DeviceInfo info;
			info.name = midiIn.getPortName(i);
			info.portNumber = static_cast<int>(i);
			devices.push_back(info);
		}
	} catch (RtMidiError& error) {
		std::cerr << "[MIDI] Error enumerating MIDI devices: " << error.getMessage() << std::endl;
	}

	return devices;
}

bool MIDIInput::initMIDIInput(int deviceIndex) {
	if (!midiIn) {
		std::cerr << "[MIDI] MIDI input not initialised" << std::endl;
		return false;
	}

	closeMIDIInput();

	try {
		unsigned int portCount = midiIn->getPortCount();
		if (deviceIndex < 0 || static_cast<unsigned int>(deviceIndex) >= portCount) {
			std::cerr << "[MIDI] Invalid device index: " << deviceIndex << std::endl;
			return false;
		}

		midiIn->openPort(static_cast<unsigned int>(deviceIndex));
		midiIn->setCallback(&MIDIInput::midiCallback, this);
		midiIn->ignoreTypes(false, false, false);

		processor.start();
		isActive = true;

		std::cout << "[MIDI] Opened MIDI device: " << midiIn->getPortName(static_cast<unsigned int>(deviceIndex)) << std::endl;
		return true;

	} catch (RtMidiError& error) {
		std::cerr << "[MIDI] Error opening MIDI port: " << error.getMessage() << std::endl;
		isActive = false;
		return false;
	}
}

void MIDIInput::closeMIDIInput() {
	if (midiIn && isActive) {
		try {
			processor.stop();
			midiIn->closePort();
			isActive = false;
			std::cout << "[MIDI] Closed MIDI input" << std::endl;
		} catch (RtMidiError& error) {
			std::cerr << "[MIDI] Error closing MIDI port: " << error.getMessage() << std::endl;
		}
	}
}

bool MIDIInput::isMIDIInputActive() const {
	return isActive;
}

MIDIState MIDIInput::getMIDIState() const {
	return processor.getMIDIState();
}

void MIDIInput::midiCallback(double deltatime, std::vector<unsigned char>* message, void* userData) {
	(void)deltatime;

	if (!message || message->empty()) {
		return;
	}

	MIDIInput* midiInput = static_cast<MIDIInput*>(userData);
	if (!midiInput) {
		return;
	}

	MIDIEvent event = midiInput->parseMIDIMessage(*message);
	if (event.type != MIDIEventType::Unknown) {
		midiInput->processor.queueMIDIEvent(event);
	}
}

MIDIEvent MIDIInput::parseMIDIMessage(const std::vector<unsigned char>& message) {
	if (message.empty()) {
		return MIDIEvent();
	}

	unsigned char status = message[0];
	unsigned char messageType = status & MIDI_STATUS_MASK;
	unsigned char channel = status & MIDI_CHANNEL_MASK;

	unsigned char data1 = message.size() > 1 ? message[1] : 0;
	unsigned char data2 = message.size() > 2 ? message[2] : 0;

	MIDIEventType type = MIDIEventType::Unknown;

	switch (messageType) {
		case MIDI_NOTE_OFF:
			type = MIDIEventType::NoteOff;
			break;
		case MIDI_NOTE_ON:
			type = MIDIEventType::NoteOn;
			break;
		case MIDI_AFTERTOUCH:
			type = MIDIEventType::Aftertouch;
			break;
		case MIDI_CONTROL_CHANGE:
			type = MIDIEventType::ControlChange;
			break;
		case MIDI_PROGRAM_CHANGE:
			type = MIDIEventType::ProgramChange;
			break;
		case MIDI_CHANNEL_PRESSURE:
			type = MIDIEventType::ChannelPressure;
			break;
		case MIDI_PITCH_BEND:
			type = MIDIEventType::PitchBend;
			break;
		default:
			type = MIDIEventType::Unknown;
			break;
	}

	return MIDIEvent(type, channel, data1, data2);
}
