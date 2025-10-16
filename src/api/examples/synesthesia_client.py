import socket
import struct
import time
import json
import threading
import os
from typing import Optional, List, Dict, Any, Callable
from dataclasses import dataclass
from enum import IntEnum
import sys

class MessageType(IntEnum):
    DISCOVERY_REQUEST = 0x01
    DISCOVERY_RESPONSE = 0x02
    COLOUR_DATA = 0x10
    CONFIG_UPDATE = 0x20
    PING = 0x30
    PONG = 0x31
    ERROR_RESPONSE = 0xFF

class ErrorCode(IntEnum):
    SUCCESS = 0
    INVALID_MESSAGE = 1
    UNSUPPORTED_VERSION = 2
    BUFFER_OVERFLOW = 3
    TRANSPORT_ERROR = 4
    SERIALISATION_ERROR = 5

@dataclass
class ColourData:
    frequency: float
    wavelength: float
    r: float
    g: float
    b: float
    magnitude: float
    phase: float

@dataclass
class SpectralCharacteristics:
    flatness: float
    centroid: float
    spread: float
    normalised_spread: float

@dataclass
class DiscoveryResponse:
    server_name: str
    server_version: int
    ipc_port: int
    ipc_path: str
    capabilities: int

@dataclass
class ConfigUpdate:
    smoothing_enabled: bool
    smoothing_factor: float
    colour_space: int
    frequency_range_min: int
    frequency_range_max: int

class MessageHeader:
    MAGIC = 0x53594E45  # "SYNE"
    VERSION = 1
    
    def __init__(self, msg_type: MessageType, length: int, sequence: int, timestamp: int):
        self.magic = self.MAGIC
        self.version = self.VERSION
        self.type = msg_type
        self.length = length
        self.sequence = sequence
        self.timestamp = timestamp
    
    def pack(self) -> bytes:
        return struct.pack('<IBBHIQ', 
                          self.magic, self.version, self.type.value, 
                          self.length, self.sequence, self.timestamp)
    
    @classmethod
    def unpack(cls, data: bytes) -> 'MessageHeader':
        magic, version, msg_type, length, sequence, timestamp = struct.unpack('<IBBHIQ', data[:20])
        return cls(MessageType(msg_type), length, sequence, timestamp)

class SynesthesiaClient:
    DEFAULT_SOCKET_PATH = "/tmp/synesthesia_api"
    
    def __init__(self, client_name: str = "Python Demo Client"):
        self.client_name = client_name
        self.client_version = 1
        self.sequence_counter = 0
        self.unix_socket: Optional[socket.socket] = None
        
        self.connected = False
        self.socket_path = self.DEFAULT_SOCKET_PATH
        
        self.colour_data_callback: Optional[
            Callable[[List[ColourData], int, int, int, Optional[SpectralCharacteristics]], None]
        ] = None
        self.config_update_callback: Optional[Callable[[ConfigUpdate], None]] = None
        self.connection_callback: Optional[Callable[[bool, str], None]] = None
        self.error_callback: Optional[Callable[[str], None]] = None
        
        self.running = False
        self.worker_thread: Optional[threading.Thread] = None
        
    def set_colour_data_callback(
        self,
        callback: Callable[[List[ColourData], int, int, int, Optional[SpectralCharacteristics]], None]
    ):
        """Set callback for colour data updates"""
        self.colour_data_callback = callback
        
    def set_config_update_callback(self, callback: Callable[[ConfigUpdate], None]):
        """Set callback for configuration updates"""
        self.config_update_callback = callback
        
    def set_connection_callback(self, callback: Callable[[bool, str], None]):
        """Set callback for connection status changes"""
        self.connection_callback = callback
        
    def set_error_callback(self, callback: Callable[[str], None]):
        """Set callback for error messages"""
        self.error_callback = callback
    
    def _get_timestamp(self) -> int:
        """Get current timestamp in microseconds"""
        return int(time.time() * 1_000_000)
    
    def _next_sequence(self) -> int:
        """Get next sequence number"""
        self.sequence_counter += 1
        return self.sequence_counter
    
    def find_server(self, socket_path: Optional[str] = None) -> bool:
        """Check if Synesthesia server socket exists"""
        if socket_path:
            self.socket_path = socket_path
        
        if os.path.exists(self.socket_path):
            print(f"Found server socket at {self.socket_path}")
            return True
        else:
            print(f"No server socket found at {self.socket_path}")
            if self.error_callback:
                self.error_callback(f"Server socket not found at {self.socket_path}")
            return False
    
    def set_socket_path(self, socket_path: str):
        """Set custom socket path for server connection"""
        self.socket_path = socket_path
        print(f"Socket path set to: {socket_path}")
    
    def connect(self, socket_path: Optional[str] = None) -> bool:
        """Connect to server via Unix Domain Socket"""
        if socket_path:
            self.socket_path = socket_path
        
        try:
            self.unix_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.unix_socket.connect(self.socket_path)
            self.connected = True
            
            print(f"Connected to server at {self.socket_path}")
            
            if self.connection_callback:
                self.connection_callback(True, self.socket_path)

            self.running = True
            self.worker_thread = threading.Thread(target=self._worker_loop, daemon=True)
            self.worker_thread.start()
            
            return True
            
        except Exception as e:
            if self.error_callback:
                self.error_callback(f"Connection error: {e}")
            print(f"Connection error: {e}")
            self.connected = False
            return False
    
    def disconnect(self):
        """Disconnect from server"""
        self.running = False
        self.connected = False
        
        if self.worker_thread:
            self.worker_thread.join(timeout=1.0)
        
        if self.unix_socket:
            try:
                self.unix_socket.close()
            except:
                pass
            self.unix_socket = None
        
        if self.connection_callback:
            self.connection_callback(False, "")
        
        print("Disconnected from server")
    
    def send_config_update(self, smoothing_enabled: bool = True, 
                          smoothing_factor: float = 0.8,
                          colour_space: int = 0,
                          freq_min: int = 20,
                          freq_max: int = 20000) -> bool:
        """Send configuration update to server"""
        if not self.connected or not self.unix_socket:
            return False
        
        try:
            header = MessageHeader(MessageType.CONFIG_UPDATE, 20,
                                 self._next_sequence(), self._get_timestamp())
            
            payload = struct.pack('<IfIII', 
                                1 if smoothing_enabled else 0,
                                smoothing_factor,
                                colour_space,
                                freq_min,
                                freq_max)
            
            message = header.pack() + payload
            self.unix_socket.send(message)
            
            print(f"Sent config update: smoothing={smoothing_enabled}, "
                  f"factor={smoothing_factor}, range={freq_min}-{freq_max}Hz")
            return True
            
        except Exception as e:
            if self.error_callback:
                self.error_callback(f"Config update error: {e}")
            return False
    
    def send_ping(self) -> bool:
        """Send ping to server"""
        if not self.connected or not self.unix_socket:
            return False
        
        try:
            header = MessageHeader(MessageType.PING, 0,
                                 self._next_sequence(), self._get_timestamp())
            
            message = header.pack()
            self.unix_socket.send(message)
            
            print("Sent ping")
            return True
            
        except Exception as e:
            if self.error_callback:
                self.error_callback(f"Ping error: {e}")
            return False
    
    def _worker_loop(self):
        """Main worker loop for receiving messages"""
        buffer = b''
        
        while self.running and self.connected:
            try:
                data = self.unix_socket.recv(4096)
                if not data:
                    break
                
                buffer += data
                
                while len(buffer) >= 20:  # Minimum header size
                    try:
                        header = MessageHeader.unpack(buffer[:20])
                        
                        if header.magic != MessageHeader.MAGIC:
                            print("Invalid magic number, discarding buffer")
                            buffer = b''
                            break
                        
                        total_size = 20 + header.length
                        if len(buffer) < total_size:
                            break  # Need more data
                        
                        payload = buffer[20:total_size]
                        buffer = buffer[total_size:]
                        
                        self._handle_message(header, payload)
                        
                    except Exception as e:
                        print(f"Message processing error: {e}")
                        buffer = b''
                        break
                
            except Exception as e:
                if self.running:
                    if self.error_callback:
                        self.error_callback(f"Worker loop error: {e}")
                    print(f"Worker loop error: {e}")
                break
        
        self.connected = False
    
    def _handle_message(self, header: MessageHeader, payload: bytes):
        """Handle received message"""
        try:
            if header.type == MessageType.COLOUR_DATA:
                self._handle_colour_data(payload)
            elif header.type == MessageType.CONFIG_UPDATE:
                self._handle_config_update(payload)
            elif header.type == MessageType.PONG:
                print("Received pong")
            elif header.type == MessageType.ERROR_RESPONSE:
                self._handle_error_response(payload)
            else:
                print(f"Unknown message type: {header.type}")
                
        except Exception as e:
            if self.error_callback:
                self.error_callback(f"Message handling error: {e}")
    
    def _handle_colour_data(self, payload: bytes):
        """Handle colour data message"""
        if len(payload) < 20:
            return
        
        sample_rate, fft_size, colour_count, frame_timestamp = struct.unpack('<IIIQ', payload[:20])

        spectral_characteristics: Optional[SpectralCharacteristics] = None
        offset = 20

        if len(payload) >= offset + 16:
            flatness, centroid, spread, normalised_spread = struct.unpack('<4f', payload[offset:offset+16])
            spectral_characteristics = SpectralCharacteristics(
                flatness=flatness,
                centroid=centroid,
                spread=spread,
                normalised_spread=normalised_spread
            )
            offset += 16
        
        colours = []
        
        for i in range(colour_count):
            if offset + 28 > len(payload):  # 7 floats * 4 bytes each
                break
            
            freq, wavelength, r, g, b, magnitude, phase = struct.unpack('<7f', payload[offset:offset+28])
            colours.append(ColourData(freq, wavelength, r, g, b, magnitude, phase))
            offset += 28
        
        if self.colour_data_callback:
            try:
                self.colour_data_callback(colours, sample_rate, fft_size, frame_timestamp, spectral_characteristics)
            except TypeError:
                self.colour_data_callback(colours, sample_rate, fft_size, frame_timestamp)
    
    def _handle_config_update(self, payload: bytes):
        """Handle configuration update message"""
        if len(payload) < 20:
            return
        
        smoothing_enabled, smoothing_factor, colour_space, freq_min, freq_max = struct.unpack('<IfIII', payload[:20])
        
        config = ConfigUpdate(
            smoothing_enabled != 0,
            smoothing_factor,
            colour_space,
            freq_min,
            freq_max
        )
        
        if self.config_update_callback:
            self.config_update_callback(config)
    
    def _handle_error_response(self, payload: bytes):
        """Handle error response message"""
        if len(payload) < 260:
            return
        
        error_code = struct.unpack('<I', payload[:4])[0]
        error_message = payload[4:260].rstrip(b'\x00').decode('utf-8')
        
        if self.error_callback:
            self.error_callback(f"Server error {error_code}: {error_message}")

def main():
    """Demo application showcasing all API functionality"""
    print("=== Synesthesia API Python Demo ===\n")
    
    stats = {
        'messages_received': 0,
        'colours_received': 0,
        'last_sample_rate': 0,
        'last_fft_size': 0,
        'config_updates': 0,
        'start_time': time.time()
    }
    
    def on_colour_data(
        colours: List[ColourData],
        sample_rate: int,
        fft_size: int,
        timestamp: int,
        spectral_characteristics: Optional[SpectralCharacteristics] = None
    ):
        stats['messages_received'] += 1
        stats['colours_received'] += len(colours)
        stats['last_sample_rate'] = sample_rate
        stats['last_fft_size'] = fft_size
        
        if stats['messages_received'] % 60 == 0:
            elapsed = time.time() - stats['start_time']
            msg_rate = stats['messages_received'] / elapsed
            
            print(f"\n--- Colour Data Stats ---")
            print(f"Messages: {stats['messages_received']} ({msg_rate:.1f}/sec)")
            print(f"Total colours: {stats['colours_received']}")
            print(f"Sample rate: {sample_rate}Hz, FFT size: {fft_size}")
            print(f"Colours in this batch: {len(colours)}")

            if spectral_characteristics is not None:
                print("Spectral characteristics:")
                print(f"  flatness: {spectral_characteristics.flatness:.4f}")
                print(f"  centroid: {spectral_characteristics.centroid:.1f}Hz")
                print(f"  spread: {spectral_characteristics.spread:.1f}Hz")
                print(f"  normalised spread: {spectral_characteristics.normalised_spread:.4f}")
            
            if colours:
                print("Sample colours:")
                for i, colour in enumerate(colours[:3]):
                    print(f"  {i+1}: {colour.frequency:.1f}Hz -> "
                          f"RGB({colour.r:.3f}, {colour.g:.3f}, {colour.b:.3f}) "
                          f"mag={colour.magnitude:.3f}")
                if len(colours) > 3:
                    print(f"  ... and {len(colours)-3} more")
    
    def on_config_update(config: ConfigUpdate):
        stats['config_updates'] += 1
        print(f"\n--- Config Update #{stats['config_updates']} ---")
        print(f"Smoothing: {'enabled' if config.smoothing_enabled else 'disabled'} "
              f"(factor: {config.smoothing_factor})")
        print(f"Colour space: {config.colour_space}")
        print(f"Frequency range: {config.frequency_range_min}-{config.frequency_range_max}Hz")
    
    def on_connection(connected: bool, info: str):
        if connected:
            print(f"✓ Connected to server: {info}")
        else:
            print("✗ Disconnected from server")
    
    def on_error(error: str):
        print(f"ERROR: {error}")
    
    client = SynesthesiaClient("Python Demo v1.0")
    client.set_colour_data_callback(on_colour_data)
    client.set_config_update_callback(on_config_update)
    client.set_connection_callback(on_connection)
    client.set_error_callback(on_error)
    
    try:
        print("Step 1: Looking for Synesthesia server...")
        if not client.find_server():
            print("Failed to find server socket. Is Synesthesia running with API enabled?")
            print(f"Expected socket path: {client.DEFAULT_SOCKET_PATH}")
            return 1
        
        print("\nStep 2: Connecting to server...")
        if not client.connect():
            print("Failed to connect to server")
            return 1
        
        print("\nStep 3: Sending initial configuration...")
        client.send_config_update(
            smoothing_enabled=True,
            smoothing_factor=0.7,
            colour_space=0,
            freq_min=50,
            freq_max=15000
        )
        
        print("\nStep 4: Testing connection with ping...")
        client.send_ping()
        
        print("\nStep 5: Listening for real-time colour data...")
        print("(Receiving data... Press Ctrl+C to stop)\n")

        config_demo_time = time.time()
        config_states = [
            (True, 0.9, 0, 20, 20000),    # High smoothing, full range
            (True, 0.3, 1, 100, 8000),   # Low smoothing, mid range, LAB
            (False, 0.0, 0, 50, 12000),  # No smoothing, RGB
        ]
        config_index = 0
        
        while True:
            time.sleep(1)

            if time.time() - config_demo_time > 10:
                config_demo_time = time.time()
                config_index = (config_index + 1) % len(config_states)
                
                enabled, factor, space, fmin, fmax = config_states[config_index]
                print(f"\n--- Demo: Changing configuration ---")
                client.send_config_update(enabled, factor, space, fmin, fmax)
            
            if int(time.time()) % 30 == 0:
                client.send_ping()
    
    except KeyboardInterrupt:
        print("\n\nShutting down...")
    
    finally:
        client.disconnect()
        elapsed = time.time() - stats['start_time']
        print(f"\n=== Session Summary ===")
        print(f"Duration: {elapsed:.1f} seconds")
        print(f"Messages received: {stats['messages_received']}")
        print(f"Colours processed: {stats['colours_received']}")
        print(f"Config updates: {stats['config_updates']}")
        if elapsed > 0:
            print(f"Average message rate: {stats['messages_received']/elapsed:.1f}/sec")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
