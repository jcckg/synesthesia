#include "midi_input.h"
#include <iostream>

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
	unsigned char messageType = status & 0xF0;
	unsigned char channel = status & 0x0F;

	unsigned char data1 = message.size() > 1 ? message[1] : 0;
	unsigned char data2 = message.size() > 2 ? message[2] : 0;

	MIDIEventType type = MIDIEventType::Unknown;

	switch (messageType) {
		case 0x80:
			type = MIDIEventType::NoteOff;
			break;
		case 0x90:
			type = MIDIEventType::NoteOn;
			break;
		case 0xA0:
			type = MIDIEventType::Aftertouch;
			break;
		case 0xB0:
			type = MIDIEventType::ControlChange;
			break;
		case 0xC0:
			type = MIDIEventType::ProgramChange;
			break;
		case 0xD0:
			type = MIDIEventType::ChannelPressure;
			break;
		case 0xE0:
			type = MIDIEventType::PitchBend;
			break;
		default:
			type = MIDIEventType::Unknown;
			break;
	}

	return MIDIEvent(type, channel, data1, data2);
}
