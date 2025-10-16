#include "midi_processor.h"

MIDIProcessor::MIDIProcessor() : writeIndex(0), readIndex(0), running(false) {
}

MIDIProcessor::~MIDIProcessor() {
	stop();
}

void MIDIProcessor::queueMIDIEvent(const MIDIEvent& event) {
	size_t currentWrite = writeIndex.load(std::memory_order_relaxed);
	size_t nextWrite = (currentWrite + 1) % QUEUE_SIZE;

	if (nextWrite != readIndex.load(std::memory_order_acquire)) {
		eventQueue[currentWrite] = event;
		writeIndex.store(nextWrite, std::memory_order_release);
		dataAvailable.notify_one();
	}
}

MIDIState MIDIProcessor::getMIDIState() const {
	std::lock_guard<std::mutex> lock(stateMutex);
	return currentState;
}

void MIDIProcessor::reset() {
	std::lock_guard<std::mutex> lock(stateMutex);
	analyser.reset();
	currentState = MIDIState();
}

void MIDIProcessor::start() {
	if (!running.load()) {
		running.store(true);
		workerThread = std::thread(&MIDIProcessor::processingThreadFunc, this);
	}
}

void MIDIProcessor::stop() {
	if (running.load()) {
		running.store(false);
		dataAvailable.notify_all();
		if (workerThread.joinable()) {
			workerThread.join();
		}
	}
}

void MIDIProcessor::processingThreadFunc() {
	while (running.load()) {
		std::unique_lock<std::mutex> lock(queueMutex);
		dataAvailable.wait(lock, [this] {
			return !running.load() || readIndex.load() != writeIndex.load();
		});

		if (!running.load()) {
			break;
		}

		while (readIndex.load(std::memory_order_acquire) != writeIndex.load(std::memory_order_acquire)) {
			size_t currentRead = readIndex.load(std::memory_order_relaxed);
			MIDIEvent event = eventQueue[currentRead];
			readIndex.store((currentRead + 1) % QUEUE_SIZE, std::memory_order_release);

			lock.unlock();
			processEvent(event);
			lock.lock();
		}
	}
}

void MIDIProcessor::processEvent(const MIDIEvent& event) {
	analyser.processEvent(event);

	std::lock_guard<std::mutex> lock(stateMutex);
	currentState = analyser.getCurrentState();
}
