#pragma once

#include <vector>
#include <string>
#include <atomic>
#include <thread>

#include "audio_input.h"
#include "fft_processor.h"

namespace CLI {

class HeadlessInterface {
public:
    HeadlessInterface();
    ~HeadlessInterface();
    
    void run(bool enableAPI = false, const std::string& preferredDevice = "");
    
private:
    std::atomic<bool> running;
    std::atomic<bool> deviceSelected;
    std::atomic<int> selectedDeviceIndex;
    std::atomic<bool> apiEnabled;
    
    std::vector<AudioInput::DeviceInfo> devices;
    AudioInput audioInput;
    
	float lastDominantFreq = -1.0f;
	size_t lastPeakCount = 0;
	float lastR = -1.0f, lastG = -1.0f, lastB = -1.0f;
	float lastLoudnessDb = -200.0f;
    
    void setupTerminal();
    void restoreTerminal();
    void displayDeviceSelection();
    void displayFrequencyInfo();
    void handleKeypress();
    void processAudioLoop();
    
    static void signalHandler(int signal);
    static HeadlessInterface* instance;
};

}
