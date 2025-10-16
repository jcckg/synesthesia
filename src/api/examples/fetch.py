import time
import sys
from synesthesia_client import SynesthesiaClient, ColourData

def main():
    print("Synesthesia API Fetch Demo")
    print("=" * 35)
    
    client = SynesthesiaClient("Fetch Demo")
    
    def on_colour_data(colours, sample_rate, fft_size, timestamp, spectral_characteristics=None):
        if colours:
            dominant = max(colours, key=lambda c: c.magnitude)
            rgb = (int(dominant.r * 255), int(dominant.g * 255), int(dominant.b * 255))
            
            print(f"{dominant.frequency:.1f}Hz → RGB{rgb} (magnitude: {dominant.magnitude:.3f})")
    
    def on_connection(connected, info):
        if connected:
            print(f"[!] Connected to Synesthesia: {info}")
        else:
            print("[!] Disconnected from Synesthesia")
    
    def on_error(error):
        print(f"[!]  Error: {error}")
    
    client.set_colour_data_callback(on_colour_data)
    client.set_connection_callback(on_connection)
    client.set_error_callback(on_error)
    
    print("[!] Discovering Synesthesia server...")

    if client.find_server():
        print("[!] Server found! Connecting...")
        
        if client.connect():
            print("[!] Connected! Listening for audio data...")
            print("   (Play some music in the background)")
            print("   (Press Ctrl+C to stop)")
            print()
            
            try:
                time.sleep(30)
            except KeyboardInterrupt:
                print("\n Stopping...")
            
        else:
            print("[!] Failed to connect to server")
            return 1
    else:
        print("[!] No Synesthesia server found")
        print("   Make sure Synesthesia is running with API enabled")
        return 1
    
    client.disconnect()
    print("[!] Disconnected cleanly")
    return 0

if __name__ == "__main__":
    sys.exit(main())
