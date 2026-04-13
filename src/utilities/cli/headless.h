#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <thread>

#include "audio_input.h"
#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "fft_processor.h"
#include "ui/smoothing/smoothing.h"

namespace CLI {

class HeadlessInterface {
public:
    HeadlessInterface();
    ~HeadlessInterface();
    
    void run(bool enableOSC = false, const std::string& preferredDevice = "",
             const std::string& oscDestination = "127.0.0.1",
             uint16_t oscSendPort = 7000, uint16_t oscReceivePort = 7001);
    
private:
    std::atomic<bool> running;
    std::atomic<bool> deviceSelected;
    std::atomic<int> selectedDeviceIndex;
    std::atomic<bool> oscEnabled;
    
    std::vector<AudioInput::DeviceInfo> devices;
    AudioInput audioInput;
    ColourCore::ColourSpace oscColourSpace = ColourCore::ColourSpace::Rec2020;
    bool oscGamutMappingEnabled = true;
    std::string oscDestination_ = "127.0.0.1";
    uint16_t oscSendPort_ = 7000;
    uint16_t oscReceivePort_ = 7001;
    
	float lastDominantFreq = -1.0f;
	size_t lastPeakCount = 0;
	float lastR = -1.0f, lastG = -1.0f, lastB = -1.0f;
	float lastLoudnessDb = -200.0f;
    
    SpringSmoother colourSmoother{8.0f, 1.0f, 0.3f};
    bool smoothingEnabled = true;
    bool manualSmoothing = false;
    float colourSmoothingSpeed = 0.6f;
    float spectrumSmoothingAmount = 0.2f;
    SpectralPresentation::Frame previousFrame;
    uint64_t previousFrameCounter = 0;
    bool hasPreviousFrame = false;
    
    void setupTerminal();
    void restoreTerminal();
    void displayDeviceSelection();
    void displayFrequencyInfo();
    void handleKeypress();
    void processAudioLoop();
    bool startOSCTransport();
    void stopOSCTransport();
    
    static void signalHandler(int signal);
    static HeadlessInterface* instance;
};

}
