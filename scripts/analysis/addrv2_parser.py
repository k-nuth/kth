#!/usr/bin/env python3
"""
addrv2 BIP155 message parser for debugging.
Usage: python3 addrv2_parser.py /path/to/payload.hex [start_entry] [end_entry]
"""

import sys

def read_compactsize(data, pos):
    """Read a Bitcoin compactsize from data at pos. Returns (value, new_pos)."""
    first = data[pos]
    if first == 0xff:
        val = int.from_bytes(data[pos+1:pos+9], 'little')
        return val, pos + 9
    elif first == 0xfe:
        val = int.from_bytes(data[pos+1:pos+5], 'little')
        return val, pos + 5
    elif first == 0xfd:
        val = int.from_bytes(data[pos+1:pos+3], 'little')
        return val, pos + 3
    else:
        return first, pos + 1

def parse_entry(data, pos):
    """Parse a single addrv2 entry. Returns dict with parsed fields."""
    start = pos

    time_val = int.from_bytes(data[pos:pos+4], 'little')
    pos += 4

    services, pos = read_compactsize(data, pos)

    network = data[pos]
    pos += 1

    addr_len, pos = read_compactsize(data, pos)

    if addr_len > 512:
        raise ValueError(f"addr_len too large: {addr_len}")

    addr = data[pos:pos+addr_len]
    pos += addr_len

    port = int.from_bytes(data[pos:pos+2], 'big')
    pos += 2

    return {
        'start': start,
        'end': pos,
        'size': pos - start,
        'time': time_val,
        'services': services,
        'network': network,
        'addr_len': addr_len,
        'addr': addr.hex(),
        'port': port
    }

def format_addr(entry):
    """Format address based on network type."""
    if entry['network'] == 1:  # IPv4
        if len(entry['addr']) == 8:
            octets = bytes.fromhex(entry['addr'])
            return '.'.join(str(b) for b in octets)
    elif entry['network'] == 2:  # IPv6
        if len(entry['addr']) == 32:
            addr_bytes = bytes.fromhex(entry['addr'])
            parts = [f"{addr_bytes[i]:02x}{addr_bytes[i+1]:02x}" for i in range(0, 16, 2)]
            return ':'.join(parts)
    return entry['addr']

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} payload.hex [start_entry] [end_entry]")
        sys.exit(1)

    hex_file = sys.argv[1]
    start_entry = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    end_entry = int(sys.argv[3]) if len(sys.argv) > 3 else None

    hex_data = open(hex_file).read().strip()
    raw = bytes.fromhex(hex_data)

    # Read count
    count, pos = read_compactsize(raw, 0)
    print(f"Payload: {len(raw)} bytes, {count} entries")
    print()

    if end_entry is None:
        end_entry = min(count, start_entry + 50)

    ok_count = 0
    broken_count = 0

    # Skip to start_entry
    for i in range(count):
        if i >= end_entry:
            break

        try:
            entry = parse_entry(raw, pos)

            # Determine status
            is_broken = entry['port'] == 0 or entry['services'] > 2**40

            if i >= start_entry:
                status = 'BROKEN' if is_broken else 'OK'
                addr_str = format_addr(entry)
                print(f"Entry {i:4d}: pos={entry['start']:5d} size={entry['size']:2d} "
                      f"net={entry['network']} port={entry['port']:5d} "
                      f"services={entry['services']:20d} [{status}]")
                if is_broken:
                    # Show raw bytes for broken entries
                    raw_bytes = ' '.join(f'{b:02x}' for b in raw[entry['start']:entry['start']+40])
                    print(f"          raw: {raw_bytes}")

            if is_broken:
                broken_count += 1
            else:
                ok_count += 1

            pos = entry['end']

        except Exception as e:
            if i >= start_entry:
                print(f"Entry {i}: ERROR at pos {pos}: {e}")
            break

    print()
    print(f"Summary: {ok_count} OK, {broken_count} BROKEN ({100*broken_count/(ok_count+broken_count):.1f}%)")

if __name__ == '__main__':
    main()
