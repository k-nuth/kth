// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <kth/infrastructure.hpp>
#include <fmt/core.h>

using namespace kth;
using ankerl::nanobench::Bench;

// Test data of various sizes
std::vector<uint8_t> generate_test_data(size_t size) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(i * 137);  // Pseudo-random pattern
    }
    return data;
}

void benchmark_base16() {
    fmt::print("\n========== BASE16 (HEXADECIMAL) ==========\n");

    // Small data (32 bytes - typical hash size)
    auto small_data = generate_test_data(32);

    // Medium data (256 bytes)
    auto medium_data = generate_test_data(256);

    // Large data (1KB)
    auto large_data = generate_test_data(1024);

    // Very large data (16KB - typical block header)
    auto xl_data = generate_test_data(16384);

    // Extra large (64KB)
    auto xxl_data = generate_test_data(65536);

    // Encoding benchmarks
    Bench().title("Base16 Encoding").relative(true)
        .run("encode 32B (hash)", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base16(data_slice(small_data.data(), small_data.size()))
            );
        })
        .run("encode 256B", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base16(data_slice(medium_data.data(), medium_data.size()))
            );
        })
        .run("encode 1KB", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base16(data_slice(large_data.data(), large_data.size()))
            );
        })
        .run("encode 16KB", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base16(data_slice(xl_data.data(), xl_data.size()))
            );
        })
        .run("encode 64KB", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base16(data_slice(xxl_data.data(), xxl_data.size()))
            );
        });

    // Prepare encoded versions for decoding benchmarks
    auto small_encoded = encode_base16(data_slice(small_data.data(), small_data.size()));
    auto medium_encoded = encode_base16(data_slice(medium_data.data(), medium_data.size()));
    auto large_encoded = encode_base16(data_slice(large_data.data(), large_data.size()));
    auto xl_encoded = encode_base16(data_slice(xl_data.data(), xl_data.size()));
    auto xxl_encoded = encode_base16(data_slice(xxl_data.data(), xxl_data.size()));

    // Decoding benchmarks
    Bench().title("Base16 Decoding").relative(true)
        .run("decode 32B (hash)", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base16(result, small_encoded)
            );
        })
        .run("decode 256B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base16(result, medium_encoded)
            );
        })
        .run("decode 1KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base16(result, large_encoded)
            );
        })
        .run("decode 16KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base16(result, xl_encoded)
            );
        })
        .run("decode 64KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base16(result, xxl_encoded)
            );
        });
}

void benchmark_base58() {
    fmt::print("\n========== BASE58 (BITCOIN ADDRESSES) ==========\n");

    // Small data (25 bytes - typical Bitcoin address payload)
    auto small_data = generate_test_data(25);

    // Medium data (128 bytes)
    auto medium_data = generate_test_data(128);

    // Large data (512 bytes)
    auto large_data = generate_test_data(512);

    // Encoding benchmarks
    Bench().title("Base58 Encoding").relative(true)
        .run("encode 25B (address)", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base58(data_slice(small_data.data(), small_data.size()))
            );
        })
        .run("encode 128B", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base58(data_slice(medium_data.data(), medium_data.size()))
            );
        })
        .run("encode 512B", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base58(data_slice(large_data.data(), large_data.size()))
            );
        });

    // Prepare encoded versions
    auto small_encoded = encode_base58(data_slice(small_data.data(), small_data.size()));
    auto medium_encoded = encode_base58(data_slice(medium_data.data(), medium_data.size()));
    auto large_encoded = encode_base58(data_slice(large_data.data(), large_data.size()));

    // Decoding benchmarks
    Bench().title("Base58 Decoding").relative(true)
        .run("decode 25B (address)", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base58(result, small_encoded)
            );
        })
        .run("decode 128B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base58(result, medium_encoded)
            );
        })
        .run("decode 512B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base58(result, large_encoded)
            );
        });
}

void benchmark_base64() {
    fmt::print("\n========== BASE64 (MIME ENCODING) ==========\n");

    // Small data (32 bytes)
    auto small_data = generate_test_data(32);

    // Medium data (256 bytes)
    auto medium_data = generate_test_data(256);

    // Large data (1KB)
    auto large_data = generate_test_data(1024);

    // Very large data (16KB)
    auto xl_data = generate_test_data(16384);

    // Extra large (64KB)
    auto xxl_data = generate_test_data(65536);

    // Encoding benchmarks
    Bench().title("Base64 Encoding").relative(true)
        .run("encode 32B", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base64(data_slice(small_data.data(), small_data.size()))
            );
        })
        .run("encode 256B", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base64(data_slice(medium_data.data(), medium_data.size()))
            );
        })
        .run("encode 1KB", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base64(data_slice(large_data.data(), large_data.size()))
            );
        })
        .run("encode 16KB", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base64(data_slice(xl_data.data(), xl_data.size()))
            );
        })
        .run("encode 64KB", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base64(data_slice(xxl_data.data(), xxl_data.size()))
            );
        });

    // Prepare encoded versions
    auto small_encoded = encode_base64(data_slice(small_data.data(), small_data.size()));
    auto medium_encoded = encode_base64(data_slice(medium_data.data(), medium_data.size()));
    auto large_encoded = encode_base64(data_slice(large_data.data(), large_data.size()));
    auto xl_encoded = encode_base64(data_slice(xl_data.data(), xl_data.size()));
    auto xxl_encoded = encode_base64(data_slice(xxl_data.data(), xxl_data.size()));

    // Decoding benchmarks
    Bench().title("Base64 Decoding").relative(true)
        .run("decode 32B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base64(result, small_encoded)
            );
        })
        .run("decode 256B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base64(result, medium_encoded)
            );
        })
        .run("decode 1KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base64(result, large_encoded)
            );
        })
        .run("decode 16KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base64(result, xl_encoded)
            );
        })
        .run("decode 64KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base64(result, xxl_encoded)
            );
        });
}

void benchmark_base85() {
    fmt::print("\n========== BASE85 (Z85 ENCODING) ==========\n");

    // Small data (32 bytes) - must be multiple of 4 for Z85
    auto small_data = generate_test_data(32);

    // Medium data (256 bytes)
    auto medium_data = generate_test_data(256);

    // Large data (1KB)
    auto large_data = generate_test_data(1024);

    // Encoding benchmarks
    Bench().title("Base85 Encoding").relative(true)
        .run("encode 32B", [&] {
            std::string result;
            ankerl::nanobench::doNotOptimizeAway(
                encode_base85(result, data_slice(small_data.data(), small_data.size()))
            );
        })
        .run("encode 256B", [&] {
            std::string result;
            ankerl::nanobench::doNotOptimizeAway(
                encode_base85(result, data_slice(medium_data.data(), medium_data.size()))
            );
        })
        .run("encode 1KB", [&] {
            std::string result;
            ankerl::nanobench::doNotOptimizeAway(
                encode_base85(result, data_slice(large_data.data(), large_data.size()))
            );
        });

    // Prepare encoded versions
    std::string small_encoded, medium_encoded, large_encoded;
    encode_base85(small_encoded, data_slice(small_data.data(), small_data.size()));
    encode_base85(medium_encoded, data_slice(medium_data.data(), medium_data.size()));
    encode_base85(large_encoded, data_slice(large_data.data(), large_data.size()));

    // Decoding benchmarks
    Bench().title("Base85 Decoding").relative(true)
        .run("decode 32B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base85(result, small_encoded)
            );
        })
        .run("decode 256B", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base85(result, medium_encoded)
            );
        })
        .run("decode 1KB", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(
                decode_base85(result, large_encoded)
            );
        });
}

void benchmark_cross_encoding() {
    fmt::print("\n========== CROSS-ENCODING COMPARISON ==========\n");

    // Use 256 bytes for fair comparison
    auto test_data = generate_test_data(256);
    auto data = data_slice(test_data.data(), test_data.size());

    // Compare encoding speeds
    Bench().title("Encoding Comparison (256B)").relative(true)
        .run("Base16", [&] {
            ankerl::nanobench::doNotOptimizeAway(encode_base16(data));
        })
        .run("Base58", [&] {
            ankerl::nanobench::doNotOptimizeAway(encode_base58(data));
        })
        .run("Base64", [&] {
            ankerl::nanobench::doNotOptimizeAway(encode_base64(data));
        })
        .run("Base85", [&] {
            std::string result;
            ankerl::nanobench::doNotOptimizeAway(encode_base85(result, data));
        });

    // Prepare encoded versions
    auto enc16 = encode_base16(data);
    auto enc58 = encode_base58(data);
    auto enc64 = encode_base64(data);
    std::string enc85;
    encode_base85(enc85, data);

    // Compare decoding speeds
    Bench().title("Decoding Comparison (256B)").relative(true)
        .run("Base16", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(decode_base16(result, enc16));
        })
        .run("Base58", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(decode_base58(result, enc58));
        })
        .run("Base64", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(decode_base64(result, enc64));
        })
        .run("Base85", [&] {
            data_chunk result;
            ankerl::nanobench::doNotOptimizeAway(decode_base85(result, enc85));
        });
}

void benchmark_bitcoin_operations() {
    fmt::print("\n========== BITCOIN-SPECIFIC OPERATIONS ==========\n");

    // Transaction hash encoding (common operation)
    hash_digest tx_hash;
    std::fill(tx_hash.begin(), tx_hash.end(), 0xAB);

    Bench().title("Bitcoin Hash Operations").relative(true)
        .run("encode_hash (TX ID)", [&] {
            ankerl::nanobench::doNotOptimizeAway(encode_hash(tx_hash));
        });

    auto encoded_hash = encode_hash(tx_hash);

    Bench().title("Bitcoin Hash Decode").relative(true)
        .run("decode_hash (TX ID)", [&] {
            hash_digest result;
            ankerl::nanobench::doNotOptimizeAway(decode_hash(result, encoded_hash));
        });

    // Bitcoin address encoding (Base58Check)
    auto address_data = generate_test_data(25);  // Typical address size

    Bench().title("Bitcoin Address Operations").relative(true)
        .run("encode address (Base58)", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base58(data_slice(address_data.data(), address_data.size()))
            );
        });

    // Script encoding (hex is common format)
    auto script_data = generate_test_data(100);  // Typical script size

    Bench().title("Script Encoding").relative(true)
        .run("encode script (hex)", [&] {
            ankerl::nanobench::doNotOptimizeAway(
                encode_base16(data_slice(script_data.data(), script_data.size()))
            );
        });
}

int main() {
    fmt::print("==============================================\n");
    fmt::print("  Base Encoding Performance Benchmarks\n");
    fmt::print("  Using nanobench\n");
    fmt::print("==============================================\n");

    benchmark_base16();
    benchmark_base58();
    benchmark_base64();
    benchmark_base85();
    benchmark_cross_encoding();
    benchmark_bitcoin_operations();

    fmt::print("\n==============================================\n");
    fmt::print("  Benchmarks Complete!\n");
    fmt::print("==============================================\n");

    return 0;
}
