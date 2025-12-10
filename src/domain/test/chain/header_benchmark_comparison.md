# Header Implementation Benchmark Comparison

Generated: 2025-12-10 00:01:32

## Memory Layout

| Property | header_raw | header_members | Difference |
|----------|------------|----------------|------------|
| sizeof(header) | 80 bytes | 80 bytes | same |
| alignof(header) | 1 bytes | 4 bytes | +3 bytes |

## Performance Comparison

Lower ns/op is better. The 'Winner' column shows which implementation is faster.

### Construction

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Construction from Fields | 2.00 ±3.9% | 2.21 ±7.2% | +10.5% slower | **raw** |
| Construction from Raw Bytes | 1.52 ±3.2% | N/A | N/A | raw only |
| Default Construction | 0.56 ±0.8% | 0.57 ±2.2% | +1.8% slower | **raw** |

### Field Access

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Access: version | 0.56 ±0.6% | 0.56 ±1.6% | ~same | tie |
| Access: timestamp | 0.59 ±0.4% | 0.59 ±0.3% | ~same | tie |
| Access: bits | 0.59 ±0.2% | 0.59 ±0.4% | ~same | tie |
| Access: nonce | 0.59 ±0.6% | 0.59 ±0.2% | ~same | tie |
| Access: previous_block_hash | 0.85 ±1.6% | 0.84 ±0.4% | 1.2% faster | **members** |
| Access: merkle | 0.83 ±6.8% | 0.84 ±0.2% | +1.2% slower | **raw** |
| Access: ALL fields | 1.84 ±2.5% | 1.11 ±0.2% | 39.7% faster | **members** |

### Serialization

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Serialization: to_data(wire=true) | 59.47 ±3.1% | 59.93 ±0.4% | ~same | tie |
| Raw Data Access (zero-copy) | 0.54 ±2.3% | N/A | N/A | raw only |

### Deserialization

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Deserialization: from_data(wire=true) | 3.96 ±0.8% | 4.62 ±0.3% | +16.7% slower | **raw** |

### Hashing

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Hash Computation: chain::hash() | 728.32 ±1.1% | 714.20 ±0.5% | 1.9% faster | **members** |

### Copy/Move

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Copy Construction | 1.39 ±5.0% | 2.08 ±0.6% | +49.6% slower | **raw** |
| Move Construction | 2.33 ±1.2% | 2.44 ±2.7% | +4.7% slower | **raw** |

### Comparison

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Equality Comparison (==) | 1.70 ±1.8% | 1.07 ±1.2% | 37.1% faster | **members** |

### Batch Operations

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| Batch Construction (10000 headers) | 19320.63 ±3.0% | 19300.70 ±0.7% | ~same | tie |
| Batch Construction from Raw (10000 headers) | 13321.63 ±0.6% | N/A | N/A | raw only |
| Batch Hashing (10000 headers) | 7221902.67 ±2.6% | 7271025.00 ±2.0% | ~same | tie |

### Realistic Workload

| Benchmark | header_raw (ns/op) | header_members (ns/op) | Difference | Winner |
|-----------|-------------------|----------------------|------------|--------|
| IBD Simulation: construct + access + hash (2000 headers) | 1449350.00 ±0.4% | 1472833.33 ±0.8% | +1.6% slower | **raw** |

## Summary

- **header_raw wins**: 7 benchmarks
- **header_members wins**: 4 benchmarks
- **Ties** (within 1%): 7 benchmarks

### Key Observations

- `header_raw` (80 bytes): Array-based storage with zero-copy raw data access
- `header_members` (80 bytes): Traditional member-based storage

**header_raw advantages:**
- Zero-copy raw data access for serialization
- Better cache locality for sequential header processing

**header_members advantages:**
- More intuitive code structure
- Direct field access without byte extraction
