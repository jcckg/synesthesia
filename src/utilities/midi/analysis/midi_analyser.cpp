#include "midi_analyser.h"

MIDIAnalyser::MIDIAnalyser() {
	reset();
}

void MIDIAnalyser::processEvent(const MIDIEvent& event) {
	currentState.lastEvent = event;
	currentState.hasNewEvent = true;

	switch (event.type) {
		case MIDIEventType::NoteOn:
			handleNoteOn(event);
			break;
		case MIDIEventType::NoteOff:
			handleNoteOff(event);
			break;
		case MIDIEventType::ControlChange:
			handleControlChange(event);
			break;
		case MIDIEventType::PitchBend:
			handlePitchBend(event);
			break;
		case MIDIEventType::ProgramChange:
			handleProgramChange(event);
			break;
		case MIDIEventType::Aftertouch:
			handleAftertouch(event);
			break;
		case MIDIEventType::ChannelPressure:
			handleChannelPressure(event);
			break;
		default:
			break;
	}

}

MIDIState MIDIAnalyser::getCurrentState() const {
	return currentState;
}

void MIDIAnalyser::reset() {
	currentState = MIDIState();
}

void MIDIAnalyser::handleNoteOn(const MIDIEvent& event) {
	if (event.data2 > 0) {
		currentState.activeNotes[event.data1] = event.data2;
	} else {
		currentState.activeNotes.erase(event.data1);
	}
	updateChord();
}

void MIDIAnalyser::handleNoteOff(const MIDIEvent& event) {
	currentState.activeNotes.erase(event.data1);
	updateChord();
}

void MIDIAnalyser::handleControlChange(const MIDIEvent& event) {
	currentState.controlChanges[event.data1] = event.data2;
}

void MIDIAnalyser::handlePitchBend(const MIDIEvent& event) {
	currentState.pitchBend = (event.data2 << 7) | event.data1;
}

void MIDIAnalyser::handleProgramChange(const MIDIEvent& event) {
	currentState.programNumber = event.data1;
}

void MIDIAnalyser::handleAftertouch(const MIDIEvent& event) {
	(void)event;
}

void MIDIAnalyser::handleChannelPressure(const MIDIEvent& event) {
	(void)event;
}

void MIDIAnalyser::updateChord() {
	currentState.currentChord.clear();
	for (const auto& note : currentState.activeNotes) {
		currentState.currentChord.push_back(note.first);
	}
}
