# Synesthesia API

The Synesthesia API enables external applications to receive real-time audio visualisation data over local network connections.

## Overview

The API broadcasts colour data generated from live audio input, allowing client applications to synchronise visualisations or create complementary effects. Data transmission uses a binary protocol over Unix domain sockets (macOS) with sub-millisecond latency.

### Data Flow

```
Audio Input → FFT Analysis → Colour Mapping → API Server → Client Applications
```

## Protocol

Binary message protocol with structured headers and typed payloads:

- **DISCOVERY_REQUEST/RESPONSE**: Server discovery and capability negotiation
- **COLOUR_DATA**: Primary colour information with frequency and magnitude data
- **CONFIG_UPDATE**: Runtime configuration changes
- **PING/PONG**: Connection health monitoring
- **ERROR_RESPONSE**: Error handling and status codes

### Message Structure

All messages include a 20-byte header with magic number validation, version information, sequence numbers, and timestamps.

```cpp
struct MessageHeader {
    uint32_t magic;      // Protocol identifier
    uint8_t version;     // Protocol version
    MessageType type;    // Message type
    uint16_t length;     // Payload length
    uint32_t sequence;   // Message sequence
    uint64_t timestamp;  // Microsecond timestamp
};
```

### Colour Data Format

Each colour data point contains frequency analysis results and corresponding RGB values:

```cpp
struct ColourData {
    float frequency;    // Frequency in Hz
    float wavelength;   // Wavelength in nm
    float r, g, b;      // RGB values (0.0-1.0)
    float magnitude;    // Signal strength
    float phase;        // Phase information
};
```

## Integration

### Server Integration

Enable the API server within Synesthesia:

```cpp
auto& api = SynesthesiaAPIIntegration::getInstance();
API::ServerConfig config; // Uses default configuration
if (api.startServer(config)) {
    // Server running on default socket
}
```

### Client Implementation

Connect to the API server and handle colour data:

```cpp
#include "api_client.h"

using namespace Synesthesia::API;

APIClient client;
client.setColourDataCallback([](const std::vector<ColourData>& colours, 
                                uint32_t sample_rate, uint32_t fft_size, 
                                uint64_t timestamp) {
    for (const auto& colour : colours) {
        // Process colour.r, colour.g, colour.b
        // Access colour.frequency and colour.magnitude
    }
});

client.connectToServer("/tmp/synesthesia_api");
```

### Configuration Updates

Modify processing parameters at runtime:

```cpp
// Enable smoothing with 80% blend factor, RGB colour space, 20Hz-20kHz range
client.sendConfigUpdate(true, 0.8f, 0, 20, 20000);
```

## Examples

### Python (`examples/`)

- **fetch.py**: Basic Python client demonstrating connection and data retrieval
- **websocket_bridge.py**: WebSocket bridge for web applications  
- **synesthesia_client.py**: Complete Python client library with discovery and configuration support

## Compatibility

- macOS 15 or later
- Apple Silicon (and Intel processors)
- Python 3.8+ for example clients
- Unix domain socket transport only

The API is currently macOS only (at this time).