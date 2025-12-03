#include "api_client.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace Synesthesia::API;

void printColourData(const std::vector<ColourData>& colours, uint32_t sample_rate, uint32_t fft_size, uint64_t timestamp) {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n=== Colour Data Update ===\n";
    std::cout << "Sample Rate: " << sample_rate << " Hz\n";
    std::cout << "FFT Size: " << fft_size << "\n";
    std::cout << "Timestamp: " << timestamp << "\n";
    std::cout << "Colour Count: " << colours.size() << "\n";
    
    for (size_t i = 0; i < std::min(colours.size(), size_t(5)); ++i) {
        const auto& colour = colours[i];
        std::cout << "  " << i << ": "
                  << "freq=" << colour.frequency << "Hz, "
                  << "mag=" << colour.magnitude << ", "
                  << "RGB=(" << colour.r << "," << colour.g << "," << colour.b << ")\n";
    }
    
    if (colours.size() > 5) {
        std::cout << "  ... and " << (colours.size() - 5) << " more\n";
    }
}

void printConfigUpdate(const ConfigUpdate& config) {
    std::cout << "\n=== Config Update ===\n";
    std::cout << "Smoothing: " << (config.smoothing_enabled ? "enabled" : "disabled") << "\n";
    std::cout << "Smoothing Factor: " << config.smoothing_factor << "\n";
    std::cout << "Colour Space: " << config.colour_space << "\n";
    std::cout << "Frequency Range: " << config.frequency_range_min << " - " << config.frequency_range_max << " Hz\n";
}

void printConnectionStatus(bool connected, const std::string& server_info) {
    std::cout << "\n=== Connection Status ===\n";
    std::cout << "Status: " << (connected ? "Connected" : "Disconnected") << "\n";
    if (connected && !server_info.empty()) {
        std::cout << "Server: " << server_info << "\n";
    }
}

int main() {
    std::cout << "Synesthesia API Example Client\n";
    std::cout << "==============================\n\n";
    
    ClientConfig config;
    config.client_name = "Example Client";
    config.auto_reconnect = true;
    
    APIClient client(config);
    
    client.setColourDataCallback(printColourData);
    client.setConfigUpdateCallback(printConfigUpdate);
    client.setConnectionStatusCallback(printConnectionStatus);
    
    std::cout << "Attempting to discover and connect to Synesthesia server...\n";
    
    if (client.discoverAndConnect()) {
        std::cout << "Successfully connected to server!\n";
        std::cout << "Server info: " << client.getServerInfo() << "\n\n";
        
        std::cout << "Sending test configuration update...\n";
        client.sendConfigUpdate(true, 0.7f, 0, 50, 15000);
        
        std::cout << "Sending ping...\n";
        if (client.ping()) {
            std::cout << "Ping successful!\n";
        }
        
        std::cout << "\nListening for colour data for 30 seconds...\n";
        std::cout << "Press Ctrl+C to exit early.\n\n";
        
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << "." << std::flush;
        }
        
        std::cout << "\n\nDisconnecting...\n";
        client.disconnect();
        
    } else {
        std::cout << "Failed to discover or connect to server.\n";
        std::cout << "Make sure Synesthesia is running with the API enabled.\n";
        return 1;
    }
    
    std::cout << "Example client finished.\n";
    return 0;
}