// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_MATH_CHACHA20_POLY1305_HPP
#define KTH_INFRASTRUCTURE_MATH_CHACHA20_POLY1305_HPP

#include <cstddef>
#include <cstdint>
#include <optional>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

// ChaCha20, Poly1305 and their AEAD construction, as RFC 8439 defines them:
// a 32-byte key, a 96-bit nonce and a 32-bit block counter. Portable C++, no
// assembly.

namespace kth {

constexpr size_t chacha20_key_size = 32;
constexpr size_t chacha20_nonce_size = 12;
constexpr size_t poly1305_key_size = 32;
constexpr size_t poly1305_tag_size = 16;

using chacha20_key = byte_array<chacha20_key_size>;
using chacha20_nonce = byte_array<chacha20_nonce_size>;
using poly1305_key = byte_array<poly1305_key_size>;
using poly1305_tag = byte_array<poly1305_tag_size>;

/// XOR `data` with the ChaCha20 key stream that starts at block `counter`
/// (RFC 8439 2.4). Encryption and decryption are the same operation.
[[nodiscard]]
KI_API data_chunk chacha20_xor(chacha20_key const& key, uint32_t counter,
    chacha20_nonce const& nonce, byte_span data);

/// The Poly1305 tag of `message` under a one-time key (RFC 8439 2.5).
[[nodiscard]]
KI_API poly1305_tag poly1305(poly1305_key const& key, byte_span message);

/// AEAD_CHACHA20_POLY1305 (RFC 8439 2.8): the ciphertext followed by the
/// 16-byte tag over `aad` and the ciphertext.
[[nodiscard]]
KI_API data_chunk chacha20_poly1305_encrypt(chacha20_key const& key, chacha20_nonce const& nonce,
    byte_span aad, byte_span plaintext);

/// The plaintext, or nullopt when the tag does not authenticate `aad` and the
/// ciphertext (or the input is shorter than a tag). The tag is checked in
/// constant time before anything is decrypted.
[[nodiscard]]
KI_API std::optional<data_chunk> chacha20_poly1305_decrypt(chacha20_key const& key,
    chacha20_nonce const& nonce, byte_span aad, byte_span ciphertext_and_tag);

} // namespace kth

#endif // KTH_INFRASTRUCTURE_MATH_CHACHA20_POLY1305_HPP
