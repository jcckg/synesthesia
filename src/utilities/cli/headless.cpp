#include "headless.h"

#include <iostream>
#include <iomanip>
#include <termios.h>
#include <unistd.h>
#include <csignal>
#include <algorithm>
#include <chrono>
#include <thread>

#include "colour_mapper.h"

#ifdef ENABLE_API_SERVER
#include "synesthesia_api_integration.h"
#endif

namespace CLI {

HeadlessInterface* HeadlessInterface::instance = nullptr;

HeadlessInterface::HeadlessInterface() 
    : running(false), deviceSelected(false), selectedDeviceIndex(-1), apiEnabled(false) {
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

void HeadlessInterface::run(bool enableAPI, const std::string& preferredDevice) {
    running = true;
    apiEnabled = enableAPI;
    
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
    
#ifdef ENABLE_API_SERVER
    if (apiEnabled) {
        auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
        api.startServer();
        std::cout << "API Server started" << std::endl;
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
    
#ifdef ENABLE_API_SERVER
    if (apiEnabled) {
        auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
        api.stopServer();
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
#ifdef ENABLE_API_SERVER
    std::cout << "  'a' - Toggle API server (" << (apiEnabled ? "ON" : "OFF") << ")\n";
#endif
    std::cout << "  'q' - Quit\n";
    
    std::cout.flush();
}

void HeadlessInterface::displayFrequencyInfo() {
    const auto& magnitudes = audioInput.getFFTProcessor().getMagnitudesBuffer();
    const auto& phases = audioInput.getFFTProcessor().getPhaseBuffer();

    float currentDominantFreq = 0.0f;
    float currentR = 0.0f, currentG = 0.0f, currentB = 0.0f;

    if (!magnitudes.empty() && !phases.empty()) {
        auto colourResult = ColourMapper::spectrumToColour(
            magnitudes, phases, audioInput.getSampleRate(), 2.2f,
            ColourMapper::ColourSpace::Rec2020, true);

        currentDominantFreq = colourResult.dominantFrequency;
        currentR = colourResult.r;
        currentG = colourResult.g;
        currentB = colourResult.b;
    }

    bool needsRedraw = (abs(currentDominantFreq - lastDominantFreq) > 0.1f) ||
                       (abs(currentR - lastR) > 0.001f) ||
                       (abs(currentG - lastG) > 0.001f) ||
                       (abs(currentB - lastB) > 0.001f);
    
    if (needsRedraw) {
        std::cout << "\033[2J\033[H";
        
        std::cout << "=== SYNESTHESIA - FREQUENCY ANALYSIS ===\n\n";
        std::cout << "Device: " << devices[static_cast<size_t>(selectedDeviceIndex)].name << "\n\n";

        if (currentDominantFreq > 0.0f) {
            std::cout << std::fixed << std::setprecision(1);
            std::cout << "Spectral Centroid: " << currentDominantFreq << " Hz\n";
            std::cout << std::setprecision(3);
            std::cout << "RGB: (" << currentR << ", " << currentG << ", " << currentB << ")\n";
        } else {
            std::cout << "Spectral Centroid: -- Hz\n";
            std::cout << "RGB: (0.000, 0.000, 0.000)\n";
            std::cout << "\n(No significant frequencies detected)\n";
        }
        
#ifdef ENABLE_API_SERVER
        if (apiEnabled) {
            auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
            std::cout << "\nAPI Server: " << (api.isServerRunning() ? "Running" : "Stopped");
            std::cout << " | Clients: " << api.getConnectedClients().size();
            std::cout << " | FPS: " << api.getCurrentFPS() << "\n";
        }
#endif
        
        std::cout << "\nControls: 'b' - Back | ";
#ifdef ENABLE_API_SERVER
        std::cout << "'a' - Toggle API (" << (apiEnabled ? "ON" : "OFF") << ") | ";
#endif
        std::cout << "'q' - Quit\n";
        
        std::cout.flush();
        
        lastDominantFreq = currentDominantFreq;
        lastR = currentR;
        lastG = currentG;
        lastB = currentB;
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
#ifdef ENABLE_API_SERVER
            else if (ch == 'a' || ch == 'A') {
                apiEnabled = !apiEnabled;
                auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
                if (apiEnabled) {
                    api.startServer();
                } else {
                    api.stopServer();
                }
            }
#endif
        } else {
            if (ch == 'b' || ch == 'B') {
                deviceSelected = false;
                selectedDeviceIndex = 0;
            }
#ifdef ENABLE_API_SERVER
            else if (ch == 'a' || ch == 'A') {
                apiEnabled = !apiEnabled;
                auto& api = Synesthesia::SynesthesiaAPIIntegration::getInstance();
                if (apiEnabled) {
                    api.startServer();
                } else {
                    api.stopServer();
                }
            }
#endif
        }
    }
}

}