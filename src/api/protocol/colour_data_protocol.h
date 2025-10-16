#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>

namespace Synesthesia::API {

#pragma pack(push, 1)

enum class MessageType : uint8_t {
    DISCOVERY_REQUEST = 0x01,
    DISCOVERY_RESPONSE = 0x02,
    COLOUR_DATA = 0x10,
    FULL_SPECTRUM_DATA = 0x11,
    CONFIG_UPDATE = 0x20,
    PING = 0x30,
    PONG = 0x31,
    ERROR_RESPONSE = 0xFF
};

struct MessageHeader {
    uint32_t magic = 0x53594E45;
    uint8_t version = 1;
    MessageType type;
    uint16_t length;
    uint32_t sequence;
    uint64_t timestamp;
};

struct SpectralCharacteristics {
    float flatness;        // Spectral flatness (0-1): geometric mean / arithmetic mean
    float centroid;        // Spectral centroid in Hz: weighted frequency center
    float spread;          // Spectral spread in Hz: frequency bandwidth
    float normalisedSpread; // Normalised spread (0-1)
};

struct ColourData {
    float frequency;
    float wavelength;
    float r, g, b;
    float magnitude;
    float phase;
};

struct ColourDataMessage {
    MessageHeader header;
    uint32_t sample_rate;
    uint32_t fft_size;
    uint32_t colour_count;
    uint64_t frame_timestamp;
    SpectralCharacteristics spectral_characteristics;
    ColourData colours[];
};

struct SpectralBin {
    float frequency;
    float magnitude;
    float phase;
};

struct FullSpectrumMessage {
    MessageHeader header;
    uint32_t sample_rate;
    uint32_t fft_size;
    uint32_t bin_count;
    uint32_t window_type;
    float overlap_factor;
    uint64_t frame_timestamp;
    SpectralBin bins[];
};

struct DiscoveryRequest {
    MessageHeader header;
    char client_name[64];
    uint32_t client_version;
};

struct DiscoveryResponse {
    MessageHeader header;
    char server_name[64];
    uint32_t server_version;
    uint16_t ipc_port;
    char ipc_path[256];
    uint32_t capabilities;
};

struct ConfigUpdate {
    MessageHeader header;
    uint32_t smoothing_enabled;
    float smoothing_factor;
    uint32_t colour_space;
    uint32_t frequency_range_min;
    uint32_t frequency_range_max;
};

struct ErrorResponse {
    MessageHeader header;
    uint32_t error_code;
    char error_message[256];
};

#pragma pack(pop)

enum class ErrorCode : uint32_t {
    SUCCESS = 0,
    INVALID_MESSAGE = 1,
    UNSUPPORTED_VERSION = 2,
    BUFFER_OVERFLOW = 3,
    TRANSPORT_ERROR = 4,
    SERIALISATION_ERROR = 5
};

enum class Capabilities : uint32_t {
    COLOUR_DATA_STREAMING = 0x01,
    CONFIG_UPDATES = 0x02,
    REAL_TIME_DISCOVERY = 0x04,
    LAB_COLOUR_SPACE = 0x08,
    XYZ_COLOUR_SPACE = 0x10,
    FULL_SPECTRUM_STREAMING = 0x20
};

constexpr size_t MAX_MESSAGE_SIZE = 65536;
constexpr size_t MAX_COLOURS_PER_MESSAGE = (MAX_MESSAGE_SIZE - sizeof(ColourDataMessage)) / sizeof(ColourData);
constexpr size_t MAX_SPECTRUM_BINS_PER_MESSAGE = (MAX_MESSAGE_SIZE - sizeof(FullSpectrumMessage)) / sizeof(SpectralBin);
constexpr uint16_t DEFAULT_UDP_PORT = 19851;
constexpr const char* DEFAULT_PIPE_NAME = "/tmp/synesthesia_api";

}