#pragma once

#include <map>
#include <string>
#include <vector>
#include <chrono>

enum class MIDIEventType {
	NoteOn,
	NoteOff,
	ControlChange,
	PitchBend,
	ProgramChange,
	Aftertouch,
	ChannelPressure,
	Unknown
};

struct MIDIEvent {
	MIDIEventType type;
	unsigned char channel;
	unsigned char data1;
	unsigned char data2;
	std::chrono::steady_clock::time_point timestamp;

	MIDIEvent() : type(MIDIEventType::Unknown), channel(0), data1(0), data2(0) {
		timestamp = std::chrono::steady_clock::now();
	}

	MIDIEvent(MIDIEventType t, unsigned char ch, unsigned char d1, unsigned char d2)
		: type(t), channel(ch), data1(d1), data2(d2) {
		timestamp = std::chrono::steady_clock::now();
	}
};

struct MIDIState {
	std::map<unsigned char, unsigned char> activeNotes;
	std::vector<unsigned char> currentChord;
	std::map<unsigned char, unsigned char> controlChanges;
	int pitchBend;
	unsigned char programNumber;
	MIDIEvent lastEvent;
	bool hasNewEvent;

	MIDIState() : pitchBend(0), programNumber(0), hasNewEvent(false) {}
};

class MIDIAnalyser {
public:
	MIDIAnalyser();
	~MIDIAnalyser() = default;

	void processEvent(const MIDIEvent& event);
	MIDIState getCurrentState() const;
	void reset();

private:
	MIDIState currentState;

	void handleNoteOn(const MIDIEvent& event);
	void handleNoteOff(const MIDIEvent& event);
	void handleControlChange(const MIDIEvent& event);
	void handlePitchBend(const MIDIEvent& event);
	void handleProgramChange(const MIDIEvent& event);
	void handleAftertouch(const MIDIEvent& event);
	void handleChannelPressure(const MIDIEvent& event);

	void updateChord();
	void logEvent(const MIDIEvent& event);

	std::string getNoteName(unsigned char note) const;
	std::string getControlChangeName(unsigned char cc) const;
};
