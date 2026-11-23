// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Double SHA-256 of many messages with the SHA extensions, several messages in
// step. This file is compiled with the SHA and SSE4.1 instructions enabled
// whatever the target, so nothing here may run before the caller has checked
// the processor (sha256d_batch_x86_shani_usable).

#include <kth/infrastructure/math/hash_batch.hpp>

#include "hash_batch_backends.hpp"

#include <cstdint>
#include <cstring>

#include <immintrin.h>

namespace kth::detail {

namespace {

// A core finishes one round pair of a SHA-256 state and has to wait for its
// result before the next. With several independent states it starts the next
// state's round pair meanwhile. Measured on real transactions, four in step
// hash 43% more per second than one; five or more gain nothing further.
constexpr int lanes = 4;

alignas(16) constexpr uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t initial_state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

struct states {
    // Zeroed: every lane is loaded by compress, the ones given no message too.
    uint32_t words[lanes][8] = {};

    void reset(int lane) { std::memcpy(words[lane], initial_state, sizeof initial_state); }

    void store(int lane, hash_digest& out) const {
        for (int i = 0; i < 8; ++i) {
            auto const w = words[lane][i];
            out[4 * i] = uint8_t(w >> 24);
            out[4 * i + 1] = uint8_t(w >> 16);
            out[4 * i + 2] = uint8_t(w >> 8);
            out[4 * i + 3] = uint8_t(w);
        }
    }

    // One 64-byte block for each lane. Every lane is given a block; the lanes
    // not in `active` are computed like the rest and keep their state.
    void compress(uint8_t const* const blocks[lanes], unsigned active) {
        __m128i const byte_swap = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);
        __m128i s0[lanes], s1[lanes], saved0[lanes], saved1[lanes], m[lanes][4];
        for (int l = 0; l < lanes; ++l) {
            __m128i tmp = _mm_loadu_si128(reinterpret_cast<__m128i const*>(&words[l][0]));
            __m128i x1 = _mm_loadu_si128(reinterpret_cast<__m128i const*>(&words[l][4]));
            tmp = _mm_shuffle_epi32(tmp, 0xB1);
            x1 = _mm_shuffle_epi32(x1, 0x1B);
            s0[l] = _mm_alignr_epi8(tmp, x1, 8);
            s1[l] = _mm_blend_epi16(x1, tmp, 0xF0);
            saved0[l] = s0[l];
            saved1[l] = s1[l];
        }
        // Sixteen groups of four rounds. The first four take the block's words;
        // each later group derives its four words from the sixteen before.
        for (int i = 0; i < 16; ++i) {
            __m128i const k = _mm_load_si128(reinterpret_cast<__m128i const*>(&round_constants[4 * i]));
            for (int l = 0; l < lanes; ++l) {
                if (i < 4) {
                    m[l][i] = _mm_shuffle_epi8(
                        _mm_loadu_si128(reinterpret_cast<__m128i const*>(blocks[l] + 16 * i)), byte_swap);
                } else {
                    __m128i t = _mm_sha256msg1_epu32(m[l][i & 3], m[l][(i + 1) & 3]);
                    t = _mm_add_epi32(t, _mm_alignr_epi8(m[l][(i + 3) & 3], m[l][(i + 2) & 3], 4));
                    m[l][i & 3] = _mm_sha256msg2_epu32(t, m[l][(i + 3) & 3]);
                }
                __m128i msg = _mm_add_epi32(m[l][i & 3], k);
                s1[l] = _mm_sha256rnds2_epu32(s1[l], s0[l], msg);
                msg = _mm_shuffle_epi32(msg, 0x0E);
                s0[l] = _mm_sha256rnds2_epu32(s0[l], s1[l], msg);
            }
        }
        for (int l = 0; l < lanes; ++l) {
            if (((active >> l) & 1u) == 0) continue;
            __m128i a = _mm_add_epi32(s0[l], saved0[l]);
            __m128i b = _mm_add_epi32(s1[l], saved1[l]);
            __m128i const tmp = _mm_shuffle_epi32(a, 0x1B);
            b = _mm_shuffle_epi32(b, 0xB1);
            a = _mm_blend_epi16(tmp, b, 0xF0);
            b = _mm_alignr_epi8(b, tmp, 8);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&words[l][0]), a);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(&words[l][4]), b);
        }
    }
};

constexpr uint8_t zero_block[64] = {};

// SHA-256 of every message into `out`. Each lane carries one message; when a
// lane's message ends, its hash is stored and the lane takes the next message,
// so the lanes stay busy whatever the lengths.
void first_pass(std::span<byte_span const> messages, std::span<hash_digest> out) {
    struct lane_state {
        uint8_t const* next_block = nullptr;
        size_t whole_blocks = 0;
        int tail_blocks = 0;
        int tail_done = 0;
        size_t message = 0;
        bool busy = false;
        // The end of the message with its padding: one block, or two when the
        // length does not fit after the last bytes.
        uint8_t tail[128];
    };
    lane_state lane[lanes];
    states state;
    size_t next = 0;

    auto const take_next = [&](int l) {
        auto& s = lane[l];
        if (next == messages.size()) {
            s.busy = false;
            return;
        }
        auto const& message = messages[next];
        s.message = next++;
        s.next_block = message.data();
        s.whole_blocks = message.size() / 64;
        size_t const rest = message.size() - s.whole_blocks * 64;
        std::memset(s.tail, 0, sizeof s.tail);
        // An empty span may have a null data pointer, which memcpy must not
        // be given even for zero bytes.
        if (rest != 0) {
            std::memcpy(s.tail, message.data() + s.whole_blocks * 64, rest);
        }
        s.tail[rest] = 0x80;
        s.tail_blocks = rest < 56 ? 1 : 2;
        uint64_t const bits = uint64_t(message.size()) * 8;
        for (int i = 0; i < 8; ++i) {
            s.tail[s.tail_blocks * 64 - 1 - i] = uint8_t(bits >> (8 * i));
        }
        s.tail_done = 0;
        s.busy = true;
        state.reset(l);
    };
    for (int l = 0; l < lanes; ++l) {
        take_next(l);
    }

    while (true) {
        uint8_t const* blocks[lanes];
        unsigned active = 0;
        for (int l = 0; l < lanes; ++l) {
            auto& s = lane[l];
            if ( ! s.busy) {
                blocks[l] = zero_block;
                continue;
            }
            active |= 1u << l;
            if (s.whole_blocks > 0) {
                blocks[l] = s.next_block;
                s.next_block += 64;
                --s.whole_blocks;
            } else {
                blocks[l] = s.tail + 64 * s.tail_done;
                ++s.tail_done;
            }
        }
        if (active == 0) {
            break;
        }
        state.compress(blocks, active);
        for (int l = 0; l < lanes; ++l) {
            auto& s = lane[l];
            if ( ! s.busy || s.whole_blocks > 0 || s.tail_done < s.tail_blocks) continue;
            state.store(l, out[s.message]);
            take_next(l);
        }
    }
}

// SHA-256 of every hash in `hashes`, in place. A 32-byte message with its
// padding is exactly one block, so every lane does one block and is done.
void second_pass(std::span<hash_digest> hashes) {
    states state;
    uint8_t padded[lanes][64];
    for (int l = 0; l < lanes; ++l) {
        std::memset(padded[l], 0, 64);
        padded[l][32] = 0x80;
        padded[l][62] = 0x01;      // 256 bits, big-endian, in the last eight bytes
    }
    uint8_t const* const blocks[lanes] = {padded[0], padded[1], padded[2], padded[3]};
    static_assert(lanes == 4);

    for (size_t i = 0; i < hashes.size(); i += lanes) {
        size_t const count = hashes.size() - i < size_t(lanes) ? hashes.size() - i : size_t(lanes);
        for (size_t l = 0; l < count; ++l) {
            std::memcpy(padded[l], hashes[i + l].data(), 32);
            state.reset(int(l));
        }
        state.compress(blocks, (1u << count) - 1);
        for (size_t l = 0; l < count; ++l) {
            state.store(int(l), hashes[i + l]);
        }
    }
}

} // namespace

void sha256d_batch_x86_shani(std::span<byte_span const> messages, std::span<hash_digest> out) {
    first_pass(messages, out);
    second_pass(out.first(messages.size()));
}

} // namespace kth::detail
