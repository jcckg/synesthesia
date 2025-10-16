#!/usr/bin/env python3
"""
Synesthesia API WebSocket Bridge

This demo creates a WebSocket server that bridges the Synesthesia API
to web applications, allowing real-time colour data streaming to browsers.
"""

import json
import asyncio
import logging
import threading
import time
from typing import List, Dict, Set, Optional
import sys
import argparse

try:
    import websockets
    from websockets.legacy.server import WebSocketServerProtocol
    WEBSOCKETS_AVAILABLE = True
except ImportError:
    try:
        import websockets
        from websockets.server import WebSocketServerProtocol
        WEBSOCKETS_AVAILABLE = True
    except ImportError:
        print("Warning: websockets not available. Install with: pip install websockets")
        WEBSOCKETS_AVAILABLE = False

from synesthesia_client import (
    SynesthesiaClient,
    ColourData,
    ConfigUpdate,
    SpectralCharacteristics,
)

class WebSocketBridge:
    def __init__(self, host: str = "localhost", port: int = 8765):
        self.host = host
        self.port = port
        self.clients: Set[WebSocketServerProtocol] = set()
        self.synesthesia_client: Optional[SynesthesiaClient] = SynesthesiaClient("WebSocket Bridge v1.0")
        self.connected_to_synesthesia = False
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        
        self.stats = {
            'messages_sent': 0,
            'colours_processed': 0,
            'websocket_clients': 0,
            'start_time': time.time()
        }
        
        logging.basicConfig(level=logging.INFO)
        self.logger = logging.getLogger(__name__)
    
    def schedule_async_task(self, coro):
        """Schedule an async task from sync context"""
        if self.loop and self.loop.is_running():
            asyncio.run_coroutine_threadsafe(coro, self.loop)
        else:
            self.logger.warning("No event loop available to schedule task")
    
    async def register_client(self, websocket: WebSocketServerProtocol):
        """Register a new WebSocket client"""
        self.clients.add(websocket)
        self.stats['websocket_clients'] = len(self.clients)
        self.logger.info(f"Client connected. Total clients: {len(self.clients)}")
        welcome = {
            "type": "welcome",
            "message": "Connected to Synesthesia WebSocket Bridge",
            "server_info": {
                "host": self.host,
                "port": self.port,
                "synesthesia_connected": self.connected_to_synesthesia
            }
        }
        await websocket.send(json.dumps(welcome))
    
    async def unregister_client(self, websocket: WebSocketServerProtocol):
        """Unregister a WebSocket client"""
        self.clients.discard(websocket)
        self.stats['websocket_clients'] = len(self.clients)
        self.logger.info(f"Client disconnected. Total clients: {len(self.clients)}")
    
    async def broadcast_to_clients(self, message: Dict):
        """Broadcast message to all connected WebSocket clients"""
        if not self.clients:
            return
        
        message_json = json.dumps(message)
        disconnected_clients = set()
        
        for client in self.clients:
            try:
                await client.send(message_json)
                self.stats['messages_sent'] += 1
            except websockets.exceptions.ConnectionClosed:
                disconnected_clients.add(client)
            except Exception as e:
                self.logger.error(f"Error sending to client: {e}")
                disconnected_clients.add(client)
        
        for client in disconnected_clients:
            await self.unregister_client(client)
    
    async def handle_client_message(self, websocket: WebSocketServerProtocol, message: str):
        """Handle incoming message from WebSocket client"""
        try:
            data = json.loads(message)
            msg_type = data.get("type")
            
            if msg_type == "connect_synesthesia":
                await self.connect_to_synesthesia()
            elif msg_type == "disconnect_synesthesia":
                await self.disconnect_from_synesthesia()
            elif msg_type == "config_update":
                await self.send_config_to_synesthesia(data.get("config", {}))
            elif msg_type == "ping_synesthesia":
                await self.ping_synesthesia()
            elif msg_type == "get_stats":
                await self.send_stats(websocket)
            else:
                error_response = {
                    "type": "error",
                    "message": f"Unknown message type: {msg_type}"
                }
                await websocket.send(json.dumps(error_response))
                
        except json.JSONDecodeError:
            error_response = {
                "type": "error",
                "message": "Invalid JSON message"
            }
            await websocket.send(json.dumps(error_response))
        except Exception as e:
            self.logger.error(f"Error handling client message: {e}")
    
    async def send_stats(self, websocket: WebSocketServerProtocol):
        """Send statistics to a specific client"""
        elapsed = time.time() - self.stats['start_time']
        stats_message = {
            "type": "stats",
            "data": {
                **self.stats,
                "uptime": elapsed,
                "messages_per_second": self.stats['messages_sent'] / elapsed if elapsed > 0 else 0
            }
        }
        await websocket.send(json.dumps(stats_message))
    
    async def websocket_handler(self, websocket):
        """Handle WebSocket connections"""
        await self.register_client(websocket)
        
        try:
            async for message in websocket:
                await self.handle_client_message(websocket, message)
        except websockets.exceptions.ConnectionClosed:
            pass
        except Exception as e:
            self.logger.error(f"WebSocket handler error: {e}")
        finally:
            await self.unregister_client(websocket)
    
    def setup_synesthesia_client(self):
        """Setup Synesthesia client with callbacks"""
        
        def on_colour_data(
            colours: List[ColourData],
            sample_rate: int,
            fft_size: int,
            timestamp: int,
            spectral_characteristics: Optional[SpectralCharacteristics] = None
        ):
            colour_data = []
            for colour in colours:
                colour_data.append({
                    "frequency": colour.frequency,
                    "wavelength": colour.wavelength,
                    "r": colour.r,
                    "g": colour.g,
                    "b": colour.b,
                    "magnitude": colour.magnitude,
                    "phase": colour.phase
                })
            
            message = {
                "type": "colour_data",
                "data": {
                    "colours": colour_data,
                    "sample_rate": sample_rate,
                    "fft_size": fft_size,
                    "timestamp": timestamp,
                    "colour_count": len(colours)
                }
            }

            if spectral_characteristics is not None:
                message["data"]["spectral_characteristics"] = {
                    "flatness": spectral_characteristics.flatness,
                    "centroid": spectral_characteristics.centroid,
                    "spread": spectral_characteristics.spread,
                    "normalised_spread": spectral_characteristics.normalised_spread,
                }
            
            self.stats['colours_processed'] += len(colours)
            self.schedule_async_task(self.broadcast_to_clients(message))
        
        def on_config_update(config: ConfigUpdate):
            message = {
                "type": "config_update",
                "data": {
                    "smoothing_enabled": config.smoothing_enabled,
                    "smoothing_factor": config.smoothing_factor,
                    "colour_space": config.colour_space,
                    "frequency_range_min": config.frequency_range_min,
                    "frequency_range_max": config.frequency_range_max
                }
            }
            self.schedule_async_task(self.broadcast_to_clients(message))
        
        def on_connection(connected: bool, info: str):
            self.connected_to_synesthesia = connected
            message = {
                "type": "synesthesia_connection",
                "data": {
                    "connected": connected,
                    "info": info
                }
            }
            self.schedule_async_task(self.broadcast_to_clients(message))
        
        def on_error(error: str):
            message = {
                "type": "synesthesia_error",
                "data": {
                    "error": error
                }
            }
            self.schedule_async_task(self.broadcast_to_clients(message))
        
        self.synesthesia_client.set_colour_data_callback(on_colour_data)
        self.synesthesia_client.set_config_update_callback(on_config_update)
        self.synesthesia_client.set_connection_callback(on_connection)
        self.synesthesia_client.set_error_callback(on_error)
    
    async def connect_to_synesthesia(self):
        """Connect to Synesthesia server"""
        if self.connected_to_synesthesia:
            return
        
        self.logger.info("Connecting to Synesthesia...")
        
        def connect_thread():
            if self.synesthesia_client.find_server():
                if self.synesthesia_client.connect():
                    self.logger.info("Connected to Synesthesia")
                else:
                    self.logger.error("Failed to connect to Synesthesia")
            else:
                self.logger.error("Failed to find Synesthesia server socket")
        
        threading.Thread(target=connect_thread, daemon=True).start()
    
    async def disconnect_from_synesthesia(self):
        """Disconnect from Synesthesia server"""
        if self.synesthesia_client:
            self.synesthesia_client.disconnect()
            self.connected_to_synesthesia = False
            self.logger.info("Disconnected from Synesthesia")
    
    async def send_config_to_synesthesia(self, config: Dict):
        """Send configuration update to Synesthesia"""
        if not self.connected_to_synesthesia or not self.synesthesia_client:
            return
        
        smoothing_enabled = config.get("smoothing_enabled", True)
        smoothing_factor = config.get("smoothing_factor", 0.8)
        colour_space = config.get("colour_space", 0)
        freq_min = config.get("frequency_range_min", 20)
        freq_max = config.get("frequency_range_max", 20000)
        
        def send_thread():
            self.synesthesia_client.send_config_update(
                smoothing_enabled, smoothing_factor, colour_space, freq_min, freq_max
            )
        
        threading.Thread(target=send_thread, daemon=True).start()
        self.logger.info("Sent configuration update to Synesthesia")
    
    async def ping_synesthesia(self):
        """Send ping to Synesthesia server"""
        if not self.connected_to_synesthesia or not self.synesthesia_client:
            return
        
        def ping_thread():
            self.synesthesia_client.send_ping()
        
        threading.Thread(target=ping_thread, daemon=True).start()
        self.logger.info("Sent ping to Synesthesia")
    
    async def start_server(self):
        """Start the WebSocket server"""
        self.loop = asyncio.get_event_loop()
        self.setup_synesthesia_client()
        self.logger.info(f"Starting WebSocket bridge on {self.host}:{self.port}")
        
        async with websockets.serve(self.websocket_handler, self.host, self.port):
            self.logger.info("WebSocket bridge started")
            self.logger.info("Connect clients to: ws://localhost:8765")

            await self.connect_to_synesthesia()
            await asyncio.Future()

def main():
    if not WEBSOCKETS_AVAILABLE:
        print("This requires the websockets library to run!")
        print("(Install with: pip3 install websockets)")
        return 1

    parser = argparse.ArgumentParser(description="Synesthesia API WebSocket Bridge")
    parser.add_argument("--socket-path", type=str, help="Path to the Synesthesia API socket")
    parser.add_argument("--host", type=str, default="localhost", help="Host for the WebSocket server")
    parser.add_argument("--port", type=int, default=8765, help="Port for the WebSocket server")
    args = parser.parse_args()

    bridge = WebSocketBridge(host=args.host, port=args.port)

    if args.socket_path:
        bridge.synesthesia_client.set_socket_path(args.socket_path)

    try:
        asyncio.run(bridge.start_server())
    except KeyboardInterrupt:
        print("\\nShutting down WebSocket bridge...")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
