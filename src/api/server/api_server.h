#pragma once

#include "transport.h"
#include "serialisation.h"
#include "colour_data_protocol.h"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <functional>

namespace Synesthesia::API {

struct ServerConfig {
    std::string server_name = "Synesthesia";
    uint32_t server_version = 1;
    uint16_t udp_discovery_port = DEFAULT_UDP_PORT;
    std::string ipc_endpoint = DEFAULT_PIPE_NAME;
    uint32_t capabilities = static_cast<uint32_t>(Capabilities::COLOUR_DATA_STREAMING) |
                           static_cast<uint32_t>(Capabilities::CONFIG_UPDATES) |
                           static_cast<uint32_t>(Capabilities::REAL_TIME_DISCOVERY) |
                           static_cast<uint32_t>(Capabilities::LAB_COLOUR_SPACE);
    size_t max_clients = 16;
    bool enable_discovery = true;
    
    uint32_t base_fps = 60;
    uint32_t max_fps = 300;
    uint32_t idle_fps = 20;
    bool adaptive_frame_rate = true;
    bool pre_allocate_buffers = true;
    size_t buffer_pool_size = 128;
};

using ColourDataProvider = std::function<std::vector<ColourData>(uint32_t& sample_rate, uint32_t& fft_size, uint64_t& timestamp, SpectralCharacteristics& spectral_characteristics)>;
using FullSpectrumDataProvider = std::function<std::vector<SpectralBin>(uint32_t& sample_rate, uint32_t& fft_size, uint64_t& timestamp)>;
using ConfigUpdateCallback = std::function<void(const ConfigUpdate& config)>;

class APIServer {
public:
    explicit APIServer(const ServerConfig& config = {});
    ~APIServer();
    
    bool start();
    void stop();
    bool isRunning() const;
    
    void setColourDataProvider(ColourDataProvider provider);
    void setFullSpectrumDataProvider(FullSpectrumDataProvider provider);
    void setConfigUpdateCallback(ConfigUpdateCallback callback);
    
    void broadcastColourData();
    void broadcastFullSpectrumData();
    void broadcastConfigUpdate(const ConfigUpdate& config);
    void enableFullSpectrumStream(bool enable);
    
    std::vector<std::string> getConnectedClients() const;
    ServerConfig getConfig() const;
    
    uint32_t getCurrentFPS() const { return current_fps_.load(); }
    bool isHighPerformanceMode() const { return high_performance_mode_.load(); }
    float getAverageFrameTime() const;
    uint64_t getTotalFramesSent() const { return frames_sent_.load(); }

private:
    void handleDiscoveryMessage(std::span<const uint8_t> data, const std::string& sender_id);
    void handleIPCMessage(std::span<const uint8_t> data, const std::string& sender_id);
    void handleConnectionChange(const std::string& client_id, bool connected);
    void handleError(const std::string& error_message);
    
    void sendDiscoveryResponse(const std::string& client_address);
    void sendErrorResponse(const std::string& client_id, ErrorCode error_code, const std::string& message);
    
    void initialiseBufferPool();
    std::vector<uint8_t> getBuffer(size_t size);
    void returnBuffer(std::vector<uint8_t>&& buffer);
    
    ServerConfig config_;
    std::unique_ptr<ITransport> discovery_transport_;
    std::unique_ptr<ITransport> ipc_transport_;
    
    ColourDataProvider colour_data_provider_;
    FullSpectrumDataProvider full_spectrum_data_provider_;
    ConfigUpdateCallback config_update_callback_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> full_spectrum_stream_enabled_{false};
    std::atomic<uint32_t> sequence_counter_{0};
    
    mutable std::mutex clients_mutex_;
    std::vector<std::string> connected_clients_;
    
    mutable std::mutex buffer_pool_mutex_;
    std::vector<std::vector<uint8_t>> buffer_pool_;
    
    std::atomic<uint64_t> frames_sent_{0};
    std::atomic<uint32_t> current_fps_{60};
    std::atomic<bool> high_performance_mode_{false};
    std::chrono::steady_clock::time_point last_performance_log_;
    std::chrono::steady_clock::time_point last_client_check_;
    
    mutable std::mutex performance_mutex_;
    std::vector<float> recent_frame_times_;
    float average_frame_time_{0.0f};
    
    std::thread worker_thread_;
    void workerLoop();
    void updatePerformanceMetrics(float frame_time);
    uint32_t calculateOptimalFPS(size_t client_count) const;
};

}