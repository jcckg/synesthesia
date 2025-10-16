#pragma once

#include <RtMidi.h>
#include <memory>
#include <string>
#include <vector>

#include "midi_processor.h"

class MIDIInput {
public:
	struct DeviceInfo {
		std::string name;
		int portNumber;
	};

	MIDIInput();
	~MIDIInput();

	static std::vector<DeviceInfo> getMIDIInputDevices();
	bool initMIDIInput(int deviceIndex);
	void closeMIDIInput();
	bool isMIDIInputActive() const;

	MIDIState getMIDIState() const;
	MIDIProcessor& getProcessor() { return processor; }

private:
	std::unique_ptr<RtMidiIn> midiIn;
	MIDIProcessor processor;
	bool isActive;

	static void midiCallback(double deltatime, std::vector<unsigned char>* message, void* userData);
	MIDIEvent parseMIDIMessage(const std::vector<unsigned char>& message);
};
