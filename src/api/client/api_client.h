#pragma once

#include "transport.h"
#include "serialisation.h"
#include "colour_data_protocol.h"
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <functional>
#include <chrono>
#include <condition_variable>

namespace Synesthesia::API {

struct ClientConfig {
    std::string client_name = "Synesthesia Client";
    uint32_t client_version = 1;
    uint16_t discovery_port = DEFAULT_UDP_PORT;
    std::chrono::milliseconds discovery_timeout{5000};
    std::chrono::milliseconds connection_timeout{3000};
    bool auto_reconnect = true;
    std::chrono::milliseconds reconnect_interval{2000};
};

using ColourDataCallback = std::function<void(const std::vector<ColourData>& colours, uint32_t sample_rate, uint32_t fft_size, uint64_t timestamp)>;
using ConfigUpdateCallback = std::function<void(const ConfigUpdate& config)>;
using ConnectionStatusCallback = std::function<void(bool connected, const std::string& server_info)>;

class APIClient {
public:
    explicit APIClient(const ClientConfig& config = {});
    ~APIClient();
    
    bool discoverAndConnect();
    bool connectToServer(const std::string& server_endpoint);
    void disconnect();
    bool isConnected() const;
    
    bool sendConfigUpdate(bool smoothing_enabled, float smoothing_factor, uint32_t colour_space, 
                         uint32_t freq_min, uint32_t freq_max);
    bool ping();
    
    void setColourDataCallback(ColourDataCallback callback);
    void setConfigUpdateCallback(ConfigUpdateCallback callback);
    void setConnectionStatusCallback(ConnectionStatusCallback callback);
    
    std::string getServerInfo() const;
    ClientConfig getConfig() const;

private:
    void handleDiscoveryMessage(std::span<const uint8_t> data, const std::string& sender_id);
    void handleIPCMessage(std::span<const uint8_t> data, const std::string& sender_id);
    void handleConnectionChange(const std::string& server_id, bool connected);
    void handleError(const std::string& error_message);
    
    bool performDiscovery();
    void connectionWorker();
    
    ClientConfig config_;
    std::unique_ptr<ITransport> discovery_transport_;
    std::unique_ptr<ITransport> ipc_transport_;
    
    ColourDataCallback colour_data_callback_;
    ConfigUpdateCallback config_update_callback_;
    ConnectionStatusCallback connection_status_callback_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<uint32_t> sequence_counter_{0};
    
    mutable std::mutex server_info_mutex_;
    std::string server_endpoint_;
    std::string server_name_;
    uint32_t server_version_{0};
    uint32_t server_capabilities_{0};
    
    std::thread connection_thread_;
    std::chrono::steady_clock::time_point last_ping_time_;
    std::chrono::steady_clock::time_point last_data_time_;
    
    mutable std::mutex discovery_mutex_;
    std::condition_variable discovery_cv_;
    bool discovery_complete_{false};
    DiscoveryResponse discovery_result_;
};

}