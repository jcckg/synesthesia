#include "api_server.h"
#include <chrono>
#include <thread>
#include <algorithm>

namespace Synesthesia::API {

APIServer::APIServer(const ServerConfig& config) 
    : config_(config), 
      current_fps_(config.base_fps),
      last_performance_log_(std::chrono::steady_clock::now()),
      last_client_check_(std::chrono::steady_clock::now()) {
    
    ipc_transport_ = TransportFactory::createTransport(config_.ipc_endpoint, true);
    ipc_transport_->setMessageCallback(
        [this](std::span<const uint8_t> data, const std::string& sender_id) {
            handleIPCMessage(data, sender_id);
        }
    );
    ipc_transport_->setConnectionCallback(
        [this](const std::string& client_id, bool connected) {
            handleConnectionChange(client_id, connected);
        }
    );
    ipc_transport_->setErrorCallback(
        [this](const std::string& error) {
            handleError("IPC: " + error);
        }
    );
}

APIServer::~APIServer() {
    stop();
}

bool APIServer::start() {
    if (running_.load()) {
        return true;
    }
    
    if (!ipc_transport_->start()) {
        return false;
    }
    
    running_.store(true);
    
    if (config_.pre_allocate_buffers) {
        initialiseBufferPool();
    }
    
    worker_thread_ = std::thread(&APIServer::workerLoop, this);
    
    return true;
}

void APIServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    if (ipc_transport_) {
        ipc_transport_->stop();
    }
    
    std::lock_guard<std::mutex> lock(clients_mutex_);
    connected_clients_.clear();
}

bool APIServer::isRunning() const {
    return running_.load();
}

void APIServer::setColourDataProvider(ColourDataProvider provider) {
    colour_data_provider_ = std::move(provider);
}

void APIServer::setFullSpectrumDataProvider(FullSpectrumDataProvider provider) {
    full_spectrum_data_provider_ = std::move(provider);
}

void APIServer::setConfigUpdateCallback(ConfigUpdateCallback callback) {
    config_update_callback_ = std::move(callback);
}

void APIServer::broadcastColourData() {
    if (!colour_data_provider_ || !ipc_transport_) {
        return;
    }
    
    uint32_t sample_rate, fft_size;
    uint64_t timestamp;
    SpectralCharacteristics spectral_characteristics{};
    auto colours = colour_data_provider_(sample_rate, fft_size, timestamp, spectral_characteristics);

    if (colours.empty()) {
        return;
    }

    size_t colour_count = std::min(colours.size(), MAX_COLOURS_PER_MESSAGE);
    size_t message_size = sizeof(ColourDataMessage) + colour_count * sizeof(ColourData);

    auto buffer = getBuffer(message_size);

    MessageSerialiser::serialiseColourDataIntoBuffer(
        buffer, colours, spectral_characteristics, sample_rate, fft_size, timestamp, sequence_counter_.fetch_add(1)
    );
    
    ipc_transport_->broadcastMessage(std::span<const uint8_t>(buffer.data(), buffer.size()));
    
    returnBuffer(std::move(buffer));
}

void APIServer::broadcastFullSpectrumData() {
    if (!full_spectrum_data_provider_ || !ipc_transport_) {
        return;
    }
    
    uint32_t sample_rate, fft_size;
    uint64_t timestamp;
    auto bins = full_spectrum_data_provider_(sample_rate, fft_size, timestamp);
    
    if (bins.empty()) {
        return;
    }
    
    size_t bin_count = std::min(bins.size(), MAX_SPECTRUM_BINS_PER_MESSAGE);
    size_t message_size = sizeof(FullSpectrumMessage) + bin_count * sizeof(SpectralBin);
    
    auto buffer = getBuffer(message_size);
    
    MessageSerialiser::serialiseFullSpectrumDataIntoBuffer(
        buffer, bins, sample_rate, fft_size, timestamp, sequence_counter_.fetch_add(1)
    );
    
    ipc_transport_->broadcastMessage(std::span<const uint8_t>(buffer.data(), buffer.size()));
    
    returnBuffer(std::move(buffer));
}

float APIServer::getAverageFrameTime() const {
    std::lock_guard<std::mutex> lock(performance_mutex_);
    return average_frame_time_;
}

void APIServer::updatePerformanceMetrics(float frame_time) {
    std::lock_guard<std::mutex> lock(performance_mutex_);
    
    recent_frame_times_.push_back(frame_time);
    
    if (recent_frame_times_.size() > 300) {
        recent_frame_times_.erase(recent_frame_times_.begin(), recent_frame_times_.begin() + 50);
    }
    if (!recent_frame_times_.empty()) {
        float sum = 0.0f;
        for (float time : recent_frame_times_) {
            sum += time;
        }
        average_frame_time_ = sum / recent_frame_times_.size();
    }
}

uint32_t APIServer::calculateOptimalFPS(size_t client_count) const {
    if (client_count == 0) {
        return config_.idle_fps;
    } else if (client_count == 1) {
        return config_.max_fps;
    } else {
        uint32_t reduction = std::min(static_cast<uint32_t>((client_count - 1) * 30), config_.max_fps - config_.base_fps);
        uint32_t scaled_fps = config_.max_fps - reduction;
        return std::max(scaled_fps, config_.base_fps);
    }
}

void APIServer::broadcastConfigUpdate(const ConfigUpdate& config) {
    if (!ipc_transport_) {
        return;
    }
    
    auto message = MessageSerialiser::serialiseConfigUpdate(
        config.smoothing_enabled != 0,
        config.smoothing_factor,
        config.colour_space,
        config.frequency_range_min,
        config.frequency_range_max,
        sequence_counter_.fetch_add(1)
    );
    
    ipc_transport_->broadcastMessage(message);
}

void APIServer::enableFullSpectrumStream(bool enable) {
    full_spectrum_stream_enabled_.store(enable);
}

std::vector<std::string> APIServer::getConnectedClients() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return connected_clients_;
}

ServerConfig APIServer::getConfig() const {
    return config_;
}

void APIServer::handleDiscoveryMessage(std::span<const uint8_t> data, const std::string& sender_id) {
    auto message = MessageDeserialiser::deserialise(data);
    if (!message || message->type != MessageType::DISCOVERY_REQUEST) {
        return;
    }
    
    auto request = MessageDeserialiser::deserialiseDiscoveryRequest(message->payload);
    if (!request) {
        return;
    }
    
    sendDiscoveryResponse(sender_id);
}

void APIServer::handleIPCMessage(std::span<const uint8_t> data, const std::string& sender_id) {
    auto message = MessageDeserialiser::deserialise(data);
    if (!message) {
        sendErrorResponse(sender_id, ErrorCode::INVALID_MESSAGE, "Failed to parse message");
        return;
    }
    
    switch (message->type) {
        case MessageType::CONFIG_UPDATE: {
            auto config = MessageDeserialiser::deserialiseConfigUpdate(message->payload);
            if (config && config_update_callback_) {
                config_update_callback_(*config);
            } else {
                sendErrorResponse(sender_id, ErrorCode::INVALID_MESSAGE, "Invalid config update");
            }
            break;
        }
        
        case MessageType::PING: {
            auto pong = MessageSerialiser::serialiseError(ErrorCode::SUCCESS, "pong", message->sequence);
            ipc_transport_->sendMessage(pong, sender_id);
            break;
        }
        
        default:
            sendErrorResponse(sender_id, ErrorCode::INVALID_MESSAGE, "Unsupported message type");
            break;
    }
}

void APIServer::handleConnectionChange(const std::string& client_id, bool connected) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    if (connected) {
        if (connected_clients_.size() >= config_.max_clients) {
            return;
        }
        connected_clients_.push_back(client_id);
    } else {
        auto it = std::find(connected_clients_.begin(), connected_clients_.end(), client_id);
        if (it != connected_clients_.end()) {
            connected_clients_.erase(it);
        }
    }
}

void APIServer::handleError(const std::string& /* error_message */) {
}

void APIServer::sendDiscoveryResponse(const std::string& /* client_address */) {
}

void APIServer::sendErrorResponse(const std::string& client_id, ErrorCode error_code, const std::string& message) {
    if (!ipc_transport_) {
        return;
    }
    
    auto error_msg = MessageSerialiser::serialiseError(error_code, message, sequence_counter_.fetch_add(1));
    ipc_transport_->sendMessage(error_msg, client_id);
}

void APIServer::initialiseBufferPool() {
    std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
    buffer_pool_.clear();
    buffer_pool_.reserve(config_.buffer_pool_size);
    
    const size_t typical_message_size = sizeof(ColourDataMessage) + 256 * sizeof(ColourData);
    
    for (size_t i = 0; i < config_.buffer_pool_size; ++i) {
        buffer_pool_.emplace_back();
        buffer_pool_.back().reserve(typical_message_size);
    }
}

std::vector<uint8_t> APIServer::getBuffer(size_t size) {
    if (!config_.pre_allocate_buffers) {
        std::vector<uint8_t> buffer;
        buffer.reserve(size);
        return buffer;
    }
    
    std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
    
    if (!buffer_pool_.empty()) {
        auto buffer = std::move(buffer_pool_.back());
        buffer_pool_.pop_back();
        
        if (buffer.capacity() < size) {
            buffer.reserve(size);
        }
        buffer.clear();
        return buffer;
    }
    
    std::vector<uint8_t> buffer;
    buffer.reserve(size);
    return buffer;
}

void APIServer::returnBuffer(std::vector<uint8_t>&& buffer) {
    if (!config_.pre_allocate_buffers) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(buffer_pool_mutex_);
    
    if (buffer_pool_.size() < config_.buffer_pool_size) {
        buffer.clear();
        buffer_pool_.push_back(std::move(buffer));
    }
}

void APIServer::workerLoop() {
    auto client_check_interval = std::chrono::milliseconds(100);
    
    uint32_t current_target_fps = config_.base_fps;
    auto target_frame_duration = std::chrono::microseconds(1000000 / current_target_fps);
    
    while (running_.load()) {
        if (!running_.load()) {
            break;
        }

        auto frame_start = std::chrono::steady_clock::now();

        if (frame_start - last_client_check_ >= client_check_interval) {
            auto clients = getConnectedClients();
            size_t client_count = clients.size();
            
            uint32_t optimal_fps = calculateOptimalFPS(client_count);
            
            if (optimal_fps != current_target_fps) {
                current_target_fps = optimal_fps;
                target_frame_duration = std::chrono::microseconds(1000000 / current_target_fps);
                current_fps_.store(current_target_fps);
                high_performance_mode_.store(client_count > 0 && optimal_fps > config_.base_fps);
            }
            
            last_client_check_ = frame_start;
        }
        
        bool has_clients = !getConnectedClients().empty();
        if (has_clients) {
            if (full_spectrum_stream_enabled_.load()) {
                if (full_spectrum_data_provider_) {
                    broadcastFullSpectrumData();
                }
            } else {
                if (colour_data_provider_) {
                    broadcastColourData();
                }
            }
            frames_sent_.fetch_add(1);
        }
        
        auto frame_end = std::chrono::steady_clock::now();
        auto processing_time = frame_end - frame_start;
        
        float frame_time_ms = std::chrono::duration<float, std::milli>(processing_time).count();
        updatePerformanceMetrics(frame_time_ms);
        
        auto sleep_time = target_frame_duration - processing_time;
        
        if (sleep_time > std::chrono::microseconds(0)) {
            std::this_thread::sleep_for(sleep_time);
        }
        
        if (frame_end - last_performance_log_ >= std::chrono::seconds(10)) {
            last_performance_log_ = frame_end;
        }
    }
}

}
