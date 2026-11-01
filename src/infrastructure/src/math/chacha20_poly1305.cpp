// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/chacha20_poly1305.hpp>

#include <algorithm>
#include <array>
#include <bit>

namespace kth {

namespace {

uint32_t load32(uint8_t const* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

uint64_t load64(uint8_t const* p) {
    return uint64_t(load32(p)) | uint64_t(load32(p + 4)) << 32;
}

void store32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

void store64(uint8_t* p, uint64_t v) {
    store32(p, uint32_t(v));
    store32(p + 4, uint32_t(v >> 32));
}

// Overwrite key material on the stack. Through a volatile pointer, so the
// compiler cannot drop the stores as dead.
template <typename T, size_t N>
void wipe(std::array<T, N>& data) {
    auto volatile* p = data.data();
    for (size_t i = 0; i < N; ++i) {
        p[i] = T{};
    }
}

using chacha20_block = std::array<uint8_t, 64>;

void quarter_round(std::array<uint32_t, 16>& x, size_t a, size_t b, size_t c, size_t d) {
    x[a] += x[b]; x[d] = std::rotl(x[d] ^ x[a], 16);
    x[c] += x[d]; x[b] = std::rotl(x[b] ^ x[c], 12);
    x[a] += x[b]; x[d] = std::rotl(x[d] ^ x[a], 8);
    x[c] += x[d]; x[b] = std::rotl(x[b] ^ x[c], 7);
}

// RFC 8439 2.3: twenty rounds over the state, then the input added back in.
chacha20_block block_function(chacha20_key const& key, uint32_t counter,
    chacha20_nonce const& nonce) {
    std::array<uint32_t, 16> state{0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (size_t i = 0; i < 8; ++i) {
        state[4 + i] = load32(key.data() + 4 * i);
    }
    state[12] = counter;
    for (size_t i = 0; i < 3; ++i) {
        state[13 + i] = load32(nonce.data() + 4 * i);
    }

    auto working = state;
    for (int round = 0; round < 10; ++round) {
        quarter_round(working, 0, 4, 8, 12);
        quarter_round(working, 1, 5, 9, 13);
        quarter_round(working, 2, 6, 10, 14);
        quarter_round(working, 3, 7, 11, 15);
        quarter_round(working, 0, 5, 10, 15);
        quarter_round(working, 1, 6, 11, 12);
        quarter_round(working, 2, 7, 8, 13);
        quarter_round(working, 3, 4, 9, 14);
    }

    chacha20_block out;
    for (size_t i = 0; i < 16; ++i) {
        store32(out.data() + 4 * i, working[i] + state[i]);
    }
    wipe(state);
    wipe(working);
    return out;
}

// Poly1305 in three 44/44/42-bit limbs, the products in 128 bits.
struct poly1305_state {
    static constexpr uint64_t mask44 = (uint64_t{1} << 44) - 1;
    static constexpr uint64_t mask42 = (uint64_t{1} << 42) - 1;

    std::array<uint64_t, 3> r{};
    std::array<uint64_t, 3> h{};
    std::array<uint64_t, 2> pad{};

    explicit poly1305_state(poly1305_key const& key) {
        // r is clamped as RFC 8439 2.5 requires.
        auto const t0 = load64(key.data());
        auto const t1 = load64(key.data() + 8);
        r[0] = t0 & 0xffc0fffffffULL;
        r[1] = ((t0 >> 44) | (t1 << 20)) & 0xfffffc0ffffULL;
        r[2] = (t1 >> 24) & 0x00ffffffc0fULL;
        pad[0] = load64(key.data() + 16);
        pad[1] = load64(key.data() + 24);
    }

    ~poly1305_state() {
        wipe(r);
        wipe(h);
        wipe(pad);
    }

    // One 16-byte block; `high` is the 2^128 bit a full block carries.
    void block(uint8_t const* m, uint64_t high) {
        using u128 = unsigned __int128;
        auto const s1 = r[1] * (5 << 2);
        auto const s2 = r[2] * (5 << 2);
        auto const t0 = load64(m);
        auto const t1 = load64(m + 8);
        h[0] += t0 & mask44;
        h[1] += ((t0 >> 44) | (t1 << 20)) & mask44;
        h[2] += ((t1 >> 24) & mask42) | high;

        auto d0 = u128(h[0]) * r[0] + u128(h[1]) * s2 + u128(h[2]) * s1;
        auto d1 = u128(h[0]) * r[1] + u128(h[1]) * r[0] + u128(h[2]) * s2;
        auto d2 = u128(h[0]) * r[2] + u128(h[1]) * r[1] + u128(h[2]) * r[0];

        uint64_t carry = uint64_t(d0 >> 44);
        h[0] = uint64_t(d0) & mask44;
        d1 += carry;
        carry = uint64_t(d1 >> 44);
        h[1] = uint64_t(d1) & mask44;
        d2 += carry;
        carry = uint64_t(d2 >> 42);
        h[2] = uint64_t(d2) & mask42;
        h[0] += carry * 5;
        carry = h[0] >> 44;
        h[0] &= mask44;
        h[1] += carry;
    }

    void update(byte_span message) {
        auto const* m = message.data();
        auto remaining = message.size();
        for (; remaining >= 16; m += 16, remaining -= 16) {
            block(m, uint64_t{1} << 40);
        }
        if (remaining != 0) {
            // A short final block: a 1 byte after it, then zeros, and no high bit.
            std::array<uint8_t, 16> last{};
            std::copy_n(m, remaining, last.begin());
            last[remaining] = 1;
            block(last.data(), 0);
        }
    }

    poly1305_tag finish() {
        // Fully carry h.
        uint64_t carry = h[1] >> 44;
        h[1] &= mask44;
        h[2] += carry;
        carry = h[2] >> 42;
        h[2] &= mask42;
        h[0] += carry * 5;
        carry = h[0] >> 44;
        h[0] &= mask44;
        h[1] += carry;
        carry = h[1] >> 44;
        h[1] &= mask44;
        h[2] += carry;
        carry = h[2] >> 42;
        h[2] &= mask42;
        h[0] += carry * 5;
        carry = h[0] >> 44;
        h[0] &= mask44;
        h[1] += carry;

        // g = h - p, kept when h >= p (no borrow), chosen without branching.
        auto g0 = h[0] + 5;
        carry = g0 >> 44;
        g0 &= mask44;
        auto g1 = h[1] + carry;
        carry = g1 >> 44;
        g1 &= mask44;
        auto g2 = h[2] + carry - (uint64_t{1} << 42);

        auto const use_g = (g2 >> 63) - 1;   // all ones when g2 did not go negative
        g0 &= use_g;
        g1 &= use_g;
        g2 &= use_g;
        auto const use_h = ~use_g;
        h[0] = (h[0] & use_h) | g0;
        h[1] = (h[1] & use_h) | g1;
        h[2] = (h[2] & use_h) | g2;

        // h + pad, modulo 2^128.
        auto const t0 = pad[0];
        auto const t1 = pad[1];
        h[0] += t0 & mask44;
        carry = h[0] >> 44;
        h[0] &= mask44;
        h[1] += (((t0 >> 44) | (t1 << 20)) & mask44) + carry;
        carry = h[1] >> 44;
        h[1] &= mask44;
        h[2] += ((t1 >> 24) & mask42) + carry;
        h[2] &= mask42;

        poly1305_tag tag;
        store64(tag.data(), h[0] | (h[1] << 44));
        store64(tag.data() + 8, (h[1] >> 20) | (h[2] << 24));
        return tag;
    }
};

// The data the AEAD tag covers (RFC 8439 2.8): the AAD and the ciphertext,
// each padded to 16 bytes, then both lengths as 64-bit little-endian numbers.
poly1305_tag aead_tag(poly1305_key const& key, byte_span aad, byte_span ciphertext) {
    static constexpr std::array<uint8_t, 16> zeros{};
    poly1305_state mac(key);
    mac.update(aad);
    mac.update(byte_span(zeros.data(), (16 - aad.size() % 16) % 16));
    mac.update(ciphertext);
    mac.update(byte_span(zeros.data(), (16 - ciphertext.size() % 16) % 16));
    std::array<uint8_t, 16> lengths;
    store64(lengths.data(), aad.size());
    store64(lengths.data() + 8, ciphertext.size());
    mac.update(lengths);
    return mac.finish();
}

// RFC 8439 2.6: the first 32 bytes of the key stream block at counter 0.
poly1305_key one_time_key(chacha20_key const& key, chacha20_nonce const& nonce) {
    auto block = block_function(key, 0, nonce);
    poly1305_key result;
    std::copy_n(block.begin(), result.size(), result.begin());
    wipe(block);
    return result;
}

} // namespace

data_chunk chacha20_xor(chacha20_key const& key, uint32_t counter,
    chacha20_nonce const& nonce, byte_span data) {
    data_chunk out(data.begin(), data.end());
    for (size_t offset = 0; offset < out.size(); offset += 64, ++counter) {
        auto stream = block_function(key, counter, nonce);
        auto const n = std::min<size_t>(64, out.size() - offset);
        for (size_t i = 0; i < n; ++i) {
            out[offset + i] ^= stream[i];
        }
        wipe(stream);
    }
    return out;
}

poly1305_tag poly1305(poly1305_key const& key, byte_span message) {
    poly1305_state mac(key);
    mac.update(message);
    return mac.finish();
}

data_chunk chacha20_poly1305_encrypt(chacha20_key const& key, chacha20_nonce const& nonce,
    byte_span aad, byte_span plaintext) {
    auto mac_key = one_time_key(key, nonce);
    auto out = chacha20_xor(key, 1, nonce, plaintext);
    auto const tag = aead_tag(mac_key, aad, out);
    wipe(mac_key);
    out.insert(out.end(), tag.begin(), tag.end());
    return out;
}

std::optional<data_chunk> chacha20_poly1305_decrypt(chacha20_key const& key,
    chacha20_nonce const& nonce, byte_span aad, byte_span ciphertext_and_tag) {
    if (ciphertext_and_tag.size() < poly1305_tag_size) {
        return std::nullopt;
    }
    auto const ciphertext = ciphertext_and_tag.first(ciphertext_and_tag.size() - poly1305_tag_size);
    auto const received = ciphertext_and_tag.last(poly1305_tag_size);

    auto mac_key = one_time_key(key, nonce);
    auto const expected = aead_tag(mac_key, aad, ciphertext);
    wipe(mac_key);

    // Every byte is compared, so the time taken does not show where they differ.
    uint8_t difference = 0;
    for (size_t i = 0; i < poly1305_tag_size; ++i) {
        difference |= uint8_t(expected[i] ^ received[i]);
    }
    if (difference != 0) {
        return std::nullopt;
    }
    return chacha20_xor(key, 1, nonce, ciphertext);
}

} // namespace kth
