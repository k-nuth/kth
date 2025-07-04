#!/usr/bin/env python3
import hashlib

def hash256(data):
    """Calculate double SHA256 hash (like Bitcoin's hash256)"""
    if isinstance(data, str):
        data = bytes.fromhex(data)
    elif isinstance(data, int):
        data = bytes([data])
    
    first_hash = hashlib.sha256(data).digest()
    second_hash = hashlib.sha256(first_hash).digest()
    return second_hash.hex()

# Calculate hashes for the bytes we need
byte_50 = 0x50  # OP_RESERVED
byte_62 = 0x62  # decimal 98

print(f"Hash256 of byte 0x50 (OP_RESERVED): {hash256(byte_50)}")
print(f"Hash256 of byte 0x62: {hash256(byte_62)}")

# Also verify the known hash for byte 0x51 (OP_1)
byte_51 = 0x51
print(f"Hash256 of byte 0x51 (OP_1): {hash256(byte_51)}")
print(f"Expected: 953ccfa596a6c6d39e5980194539124fdcff116a571455a212baed811f585ee0")
