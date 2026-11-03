#include "midi_processor.h"

MIDIProcessor::MIDIProcessor() : writeIndex(0), readIndex(0), running(false) {}
MIDIProcessor::~MIDIProcessor() { stop(); }

void MIDIProcessor::queueMIDIEvent(const MIDIEvent& event) {
    if (!running.load(std::memory_order_acquire)) return;
    const auto current = writeIndex.load(std::memory_order_relaxed);
    const auto next = (current + 1) % QUEUE_SIZE;
    if (next == readIndex.load(std::memory_order_acquire)) {
        if (!resetRequested.exchange(true, std::memory_order_acq_rel)) dataAvailable.release();
        droppedEvents.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    eventQueue[current] = event;
    writeIndex.store(next, std::memory_order_release);
    dataAvailable.release();
}

MIDIState MIDIProcessor::getMIDIState() const {
    std::lock_guard lock(stateMutex);
    return currentState;
}

void MIDIProcessor::reset() {
    if (!resetRequested.exchange(true, std::memory_order_acq_rel)) dataAvailable.release();
}

void MIDIProcessor::start() {
    if (running.exchange(true)) return;
    while (dataAvailable.try_acquire()) {}
    writeIndex = 0;
    readIndex = 0;
    resetRequested = false;
    droppedEvents = 0;
    {
        std::lock_guard lock(stateMutex);
        analyser.reset();
        currentState = {};
    }
    workerThread = std::thread(&MIDIProcessor::processingThreadFunc, this);
}

void MIDIProcessor::stop() {
    if (!running.exchange(false)) return;
    dataAvailable.release();
    if (workerThread.joinable()) workerThread.join();
}

void MIDIProcessor::processingThreadFunc() {
    for (;;) {
        dataAvailable.acquire();
        if (!running.load(std::memory_order_acquire)) return;
        if (resetRequested.exchange(false, std::memory_order_acq_rel)) {
            readIndex.store(writeIndex.load(std::memory_order_acquire), std::memory_order_release);
            std::lock_guard lock(stateMutex);
            analyser.reset();
            currentState = {};
        }
        const auto current = readIndex.load(std::memory_order_relaxed);
        if (current == writeIndex.load(std::memory_order_acquire)) continue;
        const auto event = eventQueue[current];
        readIndex.store((current + 1) % QUEUE_SIZE, std::memory_order_release);
        processEvent(event);
    }
}

void MIDIProcessor::processEvent(const MIDIEvent& event) {
    std::lock_guard lock(stateMutex);
    analyser.processEvent(event);
    currentState = analyser.getCurrentState();
}
