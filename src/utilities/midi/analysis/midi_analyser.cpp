#include "midi_analyser.h"
#include <iostream>
#include <sstream>
#include <iomanip>

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

	logEvent(event);
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

void MIDIAnalyser::logEvent(const MIDIEvent& event) {
	std::ostringstream oss;
	oss << "[MIDI] Ch" << std::setw(2) << static_cast<int>(event.channel) << " | ";

	switch (event.type) {
		case MIDIEventType::NoteOn:
			if (event.data2 > 0) {
				oss << "Note ON:  " << getNoteName(event.data1)
					<< " (" << std::setw(3) << static_cast<int>(event.data1) << ")"
					<< " velocity " << std::setw(3) << static_cast<int>(event.data2);

				if (currentState.currentChord.size() > 1) {
					oss << " | Chord: [";
					for (size_t i = 0; i < currentState.currentChord.size(); ++i) {
						if (i > 0) oss << ", ";
						oss << getNoteName(currentState.currentChord[i]);
					}
					oss << "] (" << currentState.currentChord.size() << " notes)";
				}
			} else {
				oss << "Note OFF: " << getNoteName(event.data1)
					<< " (" << std::setw(3) << static_cast<int>(event.data1) << ")";
			}
			break;

		case MIDIEventType::NoteOff:
			oss << "Note OFF: " << getNoteName(event.data1)
				<< " (" << std::setw(3) << static_cast<int>(event.data1) << ")";
			break;

		case MIDIEventType::ControlChange:
			oss << "CC " << std::setw(3) << static_cast<int>(event.data1)
				<< " (" << getControlChangeName(event.data1) << "): "
				<< std::setw(3) << static_cast<int>(event.data2);
			break;

		case MIDIEventType::PitchBend:
			{
				int bend = currentState.pitchBend - 8192;
				oss << "Pitch Bend: " << (bend >= 0 ? "+" : "") << bend;
			}
			break;

		case MIDIEventType::ProgramChange:
			oss << "Program Change: " << std::setw(3) << static_cast<int>(event.data1);
			break;

		case MIDIEventType::Aftertouch:
			oss << "Aftertouch: Note " << static_cast<int>(event.data1)
				<< " pressure " << static_cast<int>(event.data2);
			break;

		case MIDIEventType::ChannelPressure:
			oss << "Channel Pressure: " << static_cast<int>(event.data1);
			break;

		default:
			oss << "Unknown event";
			break;
	}

	std::cout << oss.str() << std::endl;
}

std::string MIDIAnalyser::getNoteName(unsigned char note) const {
	const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	int octave = (note / 12) - 1;
	int noteIndex = note % 12;

	std::ostringstream oss;
	oss << noteNames[noteIndex] << octave;
	return oss.str();
}

std::string MIDIAnalyser::getControlChangeName(unsigned char cc) const {
	switch (cc) {
		case 1: return "Mod Wheel";
		case 2: return "Breath Controller";
		case 4: return "Foot Controller";
		case 5: return "Portamento Time";
		case 7: return "Volume";
		case 8: return "Balance";
		case 10: return "Pan";
		case 11: return "Expression";
		case 64: return "Sustain Pedal";
		case 65: return "Portamento";
		case 66: return "Sostenuto";
		case 67: return "Soft Pedal";
		case 71: return "Resonance";
		case 72: return "Release Time";
		case 73: return "Attack Time";
		case 74: return "Cutoff";
		case 84: return "Portamento Control";
		case 91: return "Reverb";
		case 93: return "Chorus";
		default: return "CC";
	}
}
