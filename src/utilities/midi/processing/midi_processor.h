#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "midi_analyser.h"

class MIDIProcessor {
public:
	MIDIProcessor();
	~MIDIProcessor();

	void queueMIDIEvent(const MIDIEvent& event);
	MIDIState getMIDIState() const;
	void reset();
	void start();
	void stop();

	MIDIAnalyser& getAnalyser() { return analyser; }

private:
	static constexpr size_t QUEUE_SIZE = 256;

	MIDIEvent eventQueue[QUEUE_SIZE];
	std::atomic<size_t> writeIndex;
	std::atomic<size_t> readIndex;
	std::thread workerThread;
	std::atomic<bool> running;
	std::condition_variable dataAvailable;
	std::mutex queueMutex;

	MIDIAnalyser analyser;
	mutable std::mutex stateMutex;
	MIDIState currentState;

	void processingThreadFunc();
	void processEvent(const MIDIEvent& event);
};
