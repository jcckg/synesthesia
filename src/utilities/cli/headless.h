#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>

#include "audio_input.h"
#include "colour_mapper.h"
#include "fft_processor.h"

namespace CLI {

class HeadlessInterface {
public:
    HeadlessInterface();
    ~HeadlessInterface();
    
    void run(bool enableOSC = false, const std::string& preferredDevice = "",
             uint16_t oscSendPort = 7000, uint16_t oscReceivePort = 7001);
    
private:
    std::atomic<bool> running;
    std::atomic<bool> deviceSelected;
    std::atomic<int> selectedDeviceIndex;
    std::atomic<bool> oscEnabled;
    
    std::vector<AudioInput::DeviceInfo> devices;
    AudioInput audioInput;
    ColourMapper::ColourSpace oscColourSpace = ColourMapper::ColourSpace::Rec2020;
    bool oscGamutMappingEnabled = true;
    uint16_t oscSendPort_ = 7000;
    uint16_t oscReceivePort_ = 7001;
    
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
