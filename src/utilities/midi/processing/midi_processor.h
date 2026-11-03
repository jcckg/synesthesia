#pragma once

#include <atomic>
#include <cstdint>
#include <semaphore>
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

    std::uint64_t getDroppedEventCount() const { return droppedEvents.load(std::memory_order_relaxed); }

private:
	static constexpr size_t QUEUE_SIZE = 256;

	MIDIEvent eventQueue[QUEUE_SIZE];
	std::atomic<size_t> writeIndex;
	std::atomic<size_t> readIndex;
	std::thread workerThread;
	std::atomic<bool> running;
    std::counting_semaphore<> dataAvailable{0};
    std::atomic<bool> resetRequested{false};
    std::atomic<std::uint64_t> droppedEvents{0};

	MIDIAnalyser analyser;
	mutable std::mutex stateMutex;
	MIDIState currentState;

	void processingThreadFunc();
	void processEvent(const MIDIEvent& event);
};
