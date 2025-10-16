#pragma once

#include "colour_data_protocol.h"
#include <vector>
#include <span>
#include <optional>

namespace Synesthesia::API {

class MessageSerialiser {
public:
    static std::vector<uint8_t> serialiseColourData(
        const std::vector<ColourData>& colours,
        const SpectralCharacteristics& spectral_characteristics,
        uint32_t sample_rate,
        uint32_t fft_size,
        uint64_t frame_timestamp,
        uint32_t sequence
    );

    static void serialiseColourDataIntoBuffer(
        std::vector<uint8_t>& buffer,
        const std::vector<ColourData>& colours,
        const SpectralCharacteristics& spectral_characteristics,
        uint32_t sample_rate,
        uint32_t fft_size,
        uint64_t frame_timestamp,
        uint32_t sequence
    );

    static void serialiseFullSpectrumDataIntoBuffer(
        std::vector<uint8_t>& buffer,
        const std::vector<SpectralBin>& bins,
        uint32_t sample_rate,
        uint32_t fft_size,
        uint64_t frame_timestamp,
        uint32_t sequence
    );
    
    static std::vector<uint8_t> serialiseDiscoveryRequest(
        const std::string& client_name,
        uint32_t client_version,
        uint32_t sequence
    );
    
    static std::vector<uint8_t> serialiseDiscoveryResponse(
        const std::string& server_name,
        uint32_t server_version,
        uint16_t ipc_port,
        const std::string& ipc_path,
        uint32_t capabilities,
        uint32_t sequence
    );
    
    static std::vector<uint8_t> serialiseConfigUpdate(
        bool smoothing_enabled,
        float smoothing_factor,
        uint32_t colour_space,
        uint32_t freq_min,
        uint32_t freq_max,
        uint32_t sequence
    );
    
    static std::vector<uint8_t> serialiseError(
        ErrorCode error_code,
        const std::string& error_message,
        uint32_t sequence
    );
};

class MessageDeserialiser {
public:
    struct DeserialisedMessage {
        MessageType type;
        uint32_t sequence;
        uint64_t timestamp;
        std::vector<uint8_t> payload;
    };
    
    static std::optional<DeserialisedMessage> deserialise(std::span<const uint8_t> data);
    
    static std::optional<std::vector<ColourData>> deserialiseColourData(
        std::span<const uint8_t> payload,
        uint32_t& sample_rate,
        uint32_t& fft_size,
        uint64_t& frame_timestamp
    );

    static std::optional<std::vector<SpectralBin>> deserialiseFullSpectrumData(
        std::span<const uint8_t> payload,
        uint32_t& sample_rate,
        uint32_t& fft_size,
        uint64_t& frame_timestamp
    );
    
    static std::optional<DiscoveryRequest> deserialiseDiscoveryRequest(
        std::span<const uint8_t> payload
    );
    
    static std::optional<DiscoveryResponse> deserialiseDiscoveryResponse(
        std::span<const uint8_t> payload
    );
    
    static std::optional<ConfigUpdate> deserialiseConfigUpdate(
        std::span<const uint8_t> payload
    );
    
    static std::optional<ErrorResponse> deserialiseError(
        std::span<const uint8_t> payload
    );

    static uint64_t getCurrentTimestamp();

private:
    static bool validateHeader(const MessageHeader& header, size_t total_size);
};

}