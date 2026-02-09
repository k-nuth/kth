#!/usr/bin/env python3
"""Monitor network download speed in real-time.

Usage: ./network_speed.py [interface] [interval_ms]
  interface: network interface (default: auto-detect)
  interval_ms: sampling interval in ms (default: 500)

Output: timestamp, MB/s, total_MB
"""

import sys
import time
from pathlib import Path

def get_default_interface():
    """Find the default network interface."""
    try:
        with open('/proc/net/route', 'r') as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 2 and parts[1] == '00000000':
                    return parts[0]
    except:
        pass
    return 'eth0'

def get_rx_bytes(interface):
    """Get received bytes for interface from /proc/net/dev."""
    try:
        with open('/proc/net/dev', 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith(interface + ':') or line.startswith(interface + ' '):
                    # Format: "interface: rx_bytes rx_packets ..." or "interface:rx_bytes ..."
                    parts = line.replace(':', ' ').split()
                    # parts[0] = interface, parts[1] = rx_bytes
                    return int(parts[1])
    except Exception as e:
        print(f"Error reading {interface}: {e}", file=sys.stderr)
    return 0

def list_interfaces():
    """List available network interfaces."""
    interfaces = []
    try:
        with open('/proc/net/dev', 'r') as f:
            for line in f:
                if ':' in line:
                    iface = line.split(':')[0].strip()
                    if iface and iface not in ('lo',):
                        interfaces.append(iface)
    except:
        pass
    return interfaces

def main():
    interface = sys.argv[1] if len(sys.argv) > 1 else get_default_interface()
    interval_ms = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    interval_sec = interval_ms / 1000.0

    # Verify interface exists
    if get_rx_bytes(interface) == 0:
        available = list_interfaces()
        print(f"Warning: {interface} not found or no traffic. Available: {available}", file=sys.stderr)

    print(f"Monitoring {interface} every {interval_ms}ms", file=sys.stderr)
    print(f"Press Ctrl+C to stop", file=sys.stderr)
    print()
    print("timestamp,MB/s,total_MB")

    last_bytes = get_rx_bytes(interface)
    last_time = time.time()
    start_bytes = last_bytes

    try:
        while True:
            time.sleep(interval_sec)

            now = time.time()
            current_bytes = get_rx_bytes(interface)

            elapsed = now - last_time
            delta_bytes = current_bytes - last_bytes
            total_mb = (current_bytes - start_bytes) / (1024 * 1024)

            if elapsed > 0:
                speed_mbps = (delta_bytes / elapsed) / (1024 * 1024)
                timestamp = time.strftime("%H:%M:%S")
                print(f"{timestamp},{speed_mbps:.1f},{total_mb:.1f}")
                sys.stdout.flush()

            last_bytes = current_bytes
            last_time = now

    except KeyboardInterrupt:
        print("\nStopped", file=sys.stderr)

if __name__ == "__main__":
    main()
