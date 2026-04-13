#include "headless.h"

#include <iostream>
#include <iomanip>
#include <termios.h>
#include <unistd.h>
#include <csignal>
#include <algorithm>
#include <chrono>
#include <thread>

#include "audio/analysis/presentation/spectral_presentation.h"
#include "colour/colour_core.h"
#include "ui/smoothing/smoothing_features.h"

#ifdef ENABLE_OSC
#include "synesthesia_osc_integration.h"
#endif

namespace CLI {

HeadlessInterface* HeadlessInterface::instance = nullptr;

HeadlessInterface::HeadlessInterface() 
    : running(false), deviceSelected(false), selectedDeviceIndex(-1), oscEnabled(false) {
    instance = this;
    devices = AudioInput::getInputDevices();
}

HeadlessInterface::~HeadlessInterface() {
    restoreTerminal();
    instance = nullptr;
}

void HeadlessInterface::signalHandler(int /* signal */) {
    if (instance) {
        instance->running = false;
    }
}

void HeadlessInterface::setupTerminal() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    
    term.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
    
    std::cout.tie(nullptr);
    std::ios_base::sync_with_stdio(false);
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

void HeadlessInterface::restoreTerminal() {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    
    term.c_lflag |= (ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
}

void HeadlessInterface::run(bool enableOSC, const std::string& preferredDevice,
                            const std::string& oscDestination,
                            const uint16_t oscSendPort, const uint16_t oscReceivePort) {
    running = true;
    oscEnabled = enableOSC;
    oscDestination_ = oscDestination;
    oscSendPort_ = oscSendPort;
    oscReceivePort_ = oscReceivePort;
    
    setupTerminal();

    std::cout << "\033[2J\033[H\033[?25l";
    if (!preferredDevice.empty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].name.find(preferredDevice) != std::string::npos) {
                selectedDeviceIndex = static_cast<int>(i);
                deviceSelected = true;
                if (audioInput.initStream(devices[i].paIndex)) {
                    std::cout << "Using preferred device: " << devices[i].name << std::endl;
                } else {
                    std::cout << "Failed to initialise preferred device, falling back to selection" << std::endl;
                    deviceSelected = false;
                    selectedDeviceIndex = -1;
                }
                break;
            }
        }
    }
    
#ifdef ENABLE_OSC
    if (oscEnabled) {
        startOSCTransport();
    }
#endif
    
    if (!deviceSelected && selectedDeviceIndex == -1) {
        selectedDeviceIndex = 0;
    }
    
    while (running) {
        if (!deviceSelected) {
            displayDeviceSelection();
        } else {
            displayFrequencyInfo();
        }
        
        handleKeypress();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    std::cout << "\033[?25h\033[2J\033[H";
    
#ifdef ENABLE_OSC
    if (oscEnabled) {
        stopOSCTransport();
    }
#endif
    
    restoreTerminal();
}

void HeadlessInterface::displayDeviceSelection() {
    std::cout << "\033[2J\033[H";
    
    std::cout << "=== SYNESTHESIA ===\n\n";
    
    if (devices.empty()) {
        std::cout << "No audio input devices found.\n";
        std::cout << "Press 'q' to quit.\n";
        std::cout.flush();
        return;
    }
    
    std::cout << "Available audio input devices:\n\n";
    
    for (size_t i = 0; i < devices.size(); ++i) {
        if (static_cast<int>(i) == selectedDeviceIndex) {
            std::cout << "  > ";
        } else {
            std::cout << "    ";
        }
        
        std::cout << i + 1 << ". " << devices[i].name;
        std::cout << " (" << devices[i].maxChannels << " channels)\n";
    }
    
    std::cout << "\n";
    std::cout << "Controls:\n";
    std::cout << "  ↑/↓ - Navigate devices\n";
    std::cout << "  Enter - Select device\n";
#ifdef ENABLE_OSC
    std::cout << "  'o' - Toggle OSC transport (" << (oscEnabled ? "ON" : "OFF") << ")\n";
#endif
    std::cout << "  'q' - Quit\n";
    
    std::cout.flush();
}

void HeadlessInterface::displayFrequencyInfo() {
	const auto& magnitudes = audioInput.getFFTProcessor().getMagnitudesBuffer();
	const auto& phases = audioInput.getFFTProcessor().getPhaseBuffer();
	const float loudnessDb = audioInput.getFFTProcessor().getMomentaryLoudnessLUFS();

    float currentDominantFreq = 0.0f;
    float currentR = 0.0f, currentG = 0.0f, currentB = 0.0f;

	float currentLoudnessDb = loudnessDb;
	if (!magnitudes.empty() && !phases.empty()) {
        SpectralPresentation::Settings settings{};
        settings.colourSpace = oscColourSpace;
        settings.applyGamutMapping = oscGamutMappingEnabled;

        SpectralPresentation::Frame frame{};
        frame.magnitudes = magnitudes;
        frame.phases = phases;
        frame.sampleRate = audioInput.getSampleRate();

        const uint64_t frameCounter = audioInput.getFFTProcessor().getFrameCounter();
        const bool hasNewFrame = !hasPreviousFrame || frameCounter != previousFrameCounter;
        const auto preparedFrame = SpectralPresentation::prepareFrame(
            frame,
            settings,
            loudnessDb,
            hasNewFrame && hasPreviousFrame ? &previousFrame : nullptr,
            frame.sampleRate > 0.0f
                ? static_cast<float>(audioInput.getFFTProcessor().getHopSize()) / frame.sampleRate
                : (1.0f / 60.0f));
        if (hasNewFrame) {
            previousFrame = frame;
            previousFrameCounter = frameCounter;
            hasPreviousFrame = true;
        }
		auto colourResult = preparedFrame.colourResult;

		currentDominantFreq = colourResult.dominantFrequency;
		currentR = colourResult.r;
		currentG = colourResult.g;
		currentB = colourResult.b;
		currentLoudnessDb = colourResult.loudnessDb;

#ifdef ENABLE_OSC
        if (oscEnabled) {
            auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
            const auto pendingSettings = osc.consumePendingSettings();
            if (pendingSettings.colourSpace.has_value()) {
                oscColourSpace = *pendingSettings.colourSpace;
            }
            if (pendingSettings.gamutMappingEnabled.has_value()) {
                oscGamutMappingEnabled = *pendingSettings.gamutMappingEnabled;
            }
            if (pendingSettings.smoothingEnabled.has_value()) {
                smoothingEnabled = *pendingSettings.smoothingEnabled;
            }
            if (pendingSettings.colourSmoothingSpeed.has_value()) {
                colourSmoothingSpeed = *pendingSettings.colourSmoothingSpeed;
                colourSmoother.setSmoothingAmount(colourSmoothingSpeed);
            }
            if (pendingSettings.spectrumSmoothingAmount.has_value()) {
                spectrumSmoothingAmount = *pendingSettings.spectrumSmoothingAmount;
            }
            
            static auto lastTime = std::chrono::steady_clock::now();
            auto currentTime = std::chrono::steady_clock::now();
            float deltaTime = std::chrono::duration_cast<std::chrono::duration<float>>(currentTime - lastTime).count();
            lastTime = currentTime;

            if (smoothingEnabled) {
                colourSmoother.setTargetColour(currentR, currentG, currentB);

                if (!manualSmoothing) {
                    auto features = ::UI::Smoothing::buildSignalFeatures(colourResult);
                    features.onsetDetected = audioInput.getFFTProcessor().getOnsetDetected();
                    features.spectralFlux = audioInput.getFFTProcessor().getSpectralFlux();
                    colourSmoother.update(deltaTime * 1.2f, features);
                } else {
                    colourSmoother.update(deltaTime * 1.2f);
                }

                colourSmoother.getCurrentColour(currentR, currentG, currentB);
            }

            osc.updateColourData(magnitudes, phases, currentDominantFreq, audioInput.getSampleRate(),
                                 currentR, currentG, currentB);
        }
#endif
	}

	bool needsRedraw = (abs(currentDominantFreq - lastDominantFreq) > 0.1f) ||
					   (abs(currentR - lastR) > 0.001f) ||
					   (abs(currentG - lastG) > 0.001f) ||
					   (abs(currentB - lastB) > 0.001f) ||
					   (abs(currentLoudnessDb - lastLoudnessDb) > 0.1f);
    
    if (needsRedraw) {
        std::cout << "\033[2J\033[H";
        
        std::cout << "=== SYNESTHESIA - FREQUENCY ANALYSIS ===\n\n";
        std::cout << "Device: " << devices[static_cast<size_t>(selectedDeviceIndex)].name << "\n\n";

		if (currentDominantFreq > 0.0f) {
			std::cout << std::fixed << std::setprecision(1);
			std::cout << "Spectral Centroid: " << currentDominantFreq << " Hz\n";
			std::cout << std::setprecision(3);
			std::cout << "RGB: (" << currentR << ", " << currentG << ", " << currentB << ")\n";
			std::cout << std::setprecision(1);
			std::cout << "Loudness: " << currentLoudnessDb << " LUFS\n";
		} else {
            std::cout << "Spectral Centroid: -- Hz\n";
            std::cout << "RGB: (0.000, 0.000, 0.000)\n";
            std::cout << "\n(No significant frequencies detected)\n";
        }
        
#ifdef ENABLE_OSC
        if (oscEnabled) {
            auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
            const auto stats = osc.getStats();
            const auto config = osc.getConfig();
            std::cout << "\nOSC: " << (osc.isRunning() ? "Running" : "Stopped");
            std::cout << " | Dest: " << config.destinationHost << ":" << config.transmitPort;
            std::cout << " | Received: " << stats.messagesReceived;
            std::cout << " | FPS: " << stats.currentFps << "\n";
        }
#endif
        
        std::cout << "\nControls: 'b' - Back | ";
#ifdef ENABLE_OSC
        std::cout << "'o' - Toggle OSC (" << (oscEnabled ? "ON" : "OFF") << ") | ";
#endif
        std::cout << "'q' - Quit\n";
        
        std::cout.flush();
        
		lastDominantFreq = currentDominantFreq;
		lastR = currentR;
		lastG = currentG;
		lastB = currentB;
		lastLoudnessDb = currentLoudnessDb;
	}
}

void HeadlessInterface::handleKeypress() {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) > 0) {
        if (ch == 'q' || ch == 'Q') {
            running = false;
            return;
        }
        
        if (!deviceSelected) {
            if (ch == '\033') {
                char seq[2];
                if (read(STDIN_FILENO, &seq, 2) == 2) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'A' && selectedDeviceIndex > 0) {
                            selectedDeviceIndex--;
                        } else if (seq[1] == 'B' && selectedDeviceIndex < static_cast<int>(devices.size()) - 1) {
                            selectedDeviceIndex++;
                        }
                    }
                }
            } else if (ch == '\n' || ch == '\r') {
                if (selectedDeviceIndex >= 0 && selectedDeviceIndex < static_cast<int>(devices.size())) {
                    if (audioInput.initStream(devices[static_cast<size_t>(selectedDeviceIndex)].paIndex)) {
                        deviceSelected = true;
                    }
                }
            }
#ifdef ENABLE_OSC
            else if (ch == 'o' || ch == 'O') {
                oscEnabled = !oscEnabled;
                if (oscEnabled) {
                    startOSCTransport();
                } else {
                    stopOSCTransport();
                }
            }
#endif
        } else {
            if (ch == 'b' || ch == 'B') {
                deviceSelected = false;
                selectedDeviceIndex = 0;
            }
#ifdef ENABLE_OSC
            else if (ch == 'o' || ch == 'O') {
                oscEnabled = !oscEnabled;
                if (oscEnabled) {
                    startOSCTransport();
                } else {
                    stopOSCTransport();
                }
            }
#endif
        }
    }
}

bool HeadlessInterface::startOSCTransport() {
#ifdef ENABLE_OSC
    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    Synesthesia::OSC::OSCConfig config;
    config.destinationHost = oscDestination_;
    config.transmitPort = oscSendPort_;
    config.receivePort = oscReceivePort_;

    if (!osc.start(config)) {
        oscEnabled = false;
        std::cout << "Failed to start OSC transport: " << osc.getLastError() << std::endl;
        return false;
    }

    oscDestination_ = osc.getConfig().destinationHost;
    std::cout << "OSC transport started (" << oscDestination_ << ":" << oscSendPort_ << ")" << std::endl;
    return true;
#else
    return false;
#endif
}

void HeadlessInterface::stopOSCTransport() {
#ifdef ENABLE_OSC
    auto& osc = Synesthesia::OSC::SynesthesiaOSCIntegration::getInstance();
    osc.stop();
#endif
}

}
