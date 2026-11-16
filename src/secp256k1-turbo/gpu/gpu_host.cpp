// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gpu_backend.hpp"

#include <memory>

#include <cstring>

#include <secp256k1_turbo/batch_verify.hpp>

#include "../src/fe52.hpp"
#include "../src/g_table.hpp"

namespace secp256k1_turbo::gpu {

namespace {

// Canonical 5x52 limbs -> 8 little-endian 32-bit limbs.
void limbs52_to_u32(std::uint64_t const l[5], u32 out[8]) {
    std::uint8_t be[32];
    fe_limbs_to_be32(l, be);
    for (int i = 0; i < 8; ++i) {
        std::uint8_t const* b = be + 28 - 4 * i;
        out[i] = (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | u32(b[3]);
    }
}

// 32 big-endian bytes -> 8 little-endian 32-bit words.
void be32_to_words(std::uint8_t const* b, u32 out[8]) {
    for (int i = 0; i < 8; ++i) {
        std::uint8_t const* q = b + 28 - 4 * i;
        out[i] = (u32(q[0]) << 24) | (u32(q[1]) << 16) | (u32(q[2]) << 8) | u32(q[3]);
    }
}

std::unique_ptr<std::uint32_t[]> build_g_words() {
    g_table const& g = get_g_table();
    auto w = std::make_unique<std::uint32_t[]>(GPU_G_WORDS);
    for (int d = 0; d < GPU_G_ENTRIES; ++d) {
        u32 xw[8], yw[8];
        limbs52_to_u32(&g.t[d][0], xw);
        limbs52_to_u32(&g.t[d][5], yw);
        for (int i = 0; i < 8; ++i) {
            w[i * GPU_G_ENTRIES + d] = xw[i];
            w[(8 + i) * GPU_G_ENTRIES + d] = yw[i];
        }
    }
    return w;
}

} // namespace

void fill_raw_input(signature_input const& in, gpu_raw_input& out) {
    std::uint8_t const* pk = in.pubkey;
    std::size_t const size = in.pubkey_size;
    u32 flags = 0;
    if (size == 33 && (pk[0] == 0x02 || pk[0] == 0x03)) {
        be32_to_words(pk + 1, out.qx);
        std::memset(out.qy, 0, sizeof(out.qy));
        flags = GPU_FLAG_OK | GPU_FLAG_DECOMPRESS | (pk[0] == 0x03 ? GPU_FLAG_PARITY_ODD : 0u);
    } else if (size == 65 && (pk[0] == 0x04 || pk[0] == 0x06 || pk[0] == 0x07)) {
        bool const odd = (pk[64] & 1) != 0;
        if (pk[0] == 0x04 || odd == (pk[0] == 0x07)) {   // hybrid: the prefix must match y
            be32_to_words(pk + 1, out.qx);
            be32_to_words(pk + 33, out.qy);
            flags = GPU_FLAG_OK | (odd ? GPU_FLAG_PARITY_ODD : 0u);
        }
    }
    if (flags == 0) {
        // Rejected on the device without reading anything else.
        std::memset(&out, 0, sizeof(out));
        return;
    }
    be32_to_words(in.sig64, out.r);
    be32_to_words(in.sig64 + 32, out.s);
    be32_to_words(in.msg32, out.z);   // the Schnorr challenge is hashed on the device
    out.flags = flags;
}

std::uint32_t const* g_table_words() {
    static std::unique_ptr<std::uint32_t[]> const w = build_g_words();
    return w.get();
}

std::uint32_t verify_one_host(gpu_raw_input const& in, bool schnorr) {
    return gpu_verify_one(&in, g_table_words(), schnorr ? 1u : 0u);
}

void field_test_host(gpu_field_test_in const& in, gpu_field_test_out& out) { gpu_field_test_one(&in, &out); }

void sha256d64_host(std::uint8_t const* in, std::size_t n, std::uint8_t* out) {
    for (std::size_t i = 0; i < n; ++i) {
        u32 pair[16];
        u32 digest[8];
        std::memcpy(pair, in + 64 * i, 64);
        gpu_sha256d_pair(pair, pair + 8, digest);
        std::memcpy(out + 32 * i, digest, 32);
    }
}

void merkle_root_host(std::uint8_t const* hashes, std::size_t n, std::uint8_t* root, bool* mutated) {
    // The same levels and the same node function as the device kernels.
    auto level = std::make_unique<u32[]>(8 * n);
    auto above = std::make_unique<u32[]>(8 * ((n + 1) / 2));
    std::memcpy(level.get(), hashes, 32 * n);
    bool equal_siblings = false;
    for (std::size_t count = n; count > 1; count = (count + 1) / 2) {
        for (std::size_t i = 0; i < (count + 1) / 2; ++i) {
            if (gpu_merkle_node(level.get(), static_cast<u32>(count), static_cast<u32>(i), above.get())) {
                equal_siblings = true;
            }
        }
        level.swap(above);
    }
    std::memcpy(root, level.get(), 32);
    if (mutated != nullptr) *mutated = equal_siblings;
}

} // namespace secp256k1_turbo::gpu
