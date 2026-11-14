// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_NOISE_HPP
#define KTH_NODE_SV2_NOISE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/sv2/framing.hpp>

namespace kth::node::sv2 {

// The EllSwift encoding is part of the transcript and ECDH input. Retain the
// actual encoding instead of regenerating one from the same curve point.
struct noise_keypair {
    [[nodiscard]] static std::optional<noise_keypair> create(
        hash_digest const& secret, hash_digest const& entropy);
    [[nodiscard]] static noise_keypair generate();
    ~noise_keypair();

    [[nodiscard]] hash_digest const& secret() const { return secret_; }
    [[nodiscard]] hash_digest const& public_key() const { return public_; }
    [[nodiscard]] byte_array<64> const& ellswift() const { return ellswift_; }

private:
    noise_keypair() = default;
    hash_digest secret_{};
    hash_digest public_{};
    byte_array<64> ellswift_{};
};

// How far an initiator's clock may be behind or ahead of the signer's when it
// checks the certificate a server shows it. The certificate carries whole
// seconds, and the reference implementation of the protocol signs from its
// current second and allows its clients the same leeway.
inline constexpr uint32_t certificate_clock_leeway_seconds = 10;

struct noise_certificate {
    static constexpr size_t size = 74;

    uint16_t version = 0;
    uint32_t valid_from = 0;
    uint32_t not_valid_after = 0;
    byte_array<64> signature{};

    [[nodiscard]] static std::optional<noise_certificate> sign(
        hash_digest const& authority_secret, hash_digest const& static_public,
        uint32_t from, uint32_t until, hash_digest const& auxiliary = {});
    // `now` may be outside the validity window by `clock_leeway` seconds. A
    // server checks its own certificate against its own clock, with none.
    [[nodiscard]] bool verify(hash_digest const& authority_public,
        hash_digest const& static_public, uint32_t now, uint32_t clock_leeway = 0) const;
    [[nodiscard]] byte_array<size> bytes() const;
    [[nodiscard]] static std::optional<noise_certificate> parse(byte_span bytes);
};

// The prefix versions the authority encoding, not the certificate (sv2-spec 4.7).
[[nodiscard]] std::string noise_authority_key(hash_digest const& public_key);
[[nodiscard]] std::optional<hash_digest> noise_parse_authority_key(std::string_view text);

struct noise_keys {
    hash_digest initiator_to_responder{};
    hash_digest responder_to_initiator{};
    ~noise_keys();
};

namespace noise_detail {

struct noise_cipher {
    hash_digest key{};
    uint64_t nonce = 0;
    ~noise_cipher();

    [[nodiscard]] std::optional<data_chunk> encrypt(byte_span ad, byte_span plaintext);
    [[nodiscard]] std::optional<data_chunk> decrypt(byte_span ad, byte_span ciphertext);
};

struct noise_symmetric_state {
    noise_symmetric_state();
    ~noise_symmetric_state();

    void mix_hash(byte_span bytes);
    void mix_key(byte_span bytes);
    [[nodiscard]] std::optional<data_chunk> encrypt(byte_span plaintext);
    [[nodiscard]] std::optional<data_chunk> decrypt(byte_span ciphertext);
    [[nodiscard]] noise_keys split() const;

    hash_digest chaining_key{};
    hash_digest hash{};
    noise_cipher cipher;
};

} // namespace noise_detail

// Each object performs one handshake. Failure consumes it, and keys become
// available only after the complete flight has been authenticated.
struct noise_initiator {
    static constexpr size_t flight_size = 64;

    noise_initiator(noise_keypair ephemeral, hash_digest authority_public, uint32_t now);
    noise_initiator(noise_initiator const&) = delete;
    noise_initiator& operator=(noise_initiator const&) = delete;
    [[nodiscard]] std::optional<data_chunk> start();
    [[nodiscard]] bool finish(byte_span response);
    [[nodiscard]] std::optional<noise_keys> const& keys() const { return keys_; }

private:
    noise_keypair ephemeral_;
    hash_digest authority_;
    uint32_t now_;
    bool started_ = false;
    bool finished_ = false;
    noise_detail::noise_symmetric_state state_;
    std::optional<noise_keys> keys_;
};

struct noise_responder {
    static constexpr size_t flight_size = 234;

    noise_responder(noise_keypair ephemeral, noise_keypair static_key,
        noise_certificate certificate);
    noise_responder(noise_responder const&) = delete;
    noise_responder& operator=(noise_responder const&) = delete;
    [[nodiscard]] std::optional<data_chunk> respond(byte_span request);
    [[nodiscard]] std::optional<noise_keys> const& keys() const { return keys_; }

private:
    noise_keypair ephemeral_;
    noise_keypair static_;
    noise_certificate certificate_;
    bool used_ = false;
    noise_detail::noise_symmetric_state state_;
    std::optional<noise_keys> keys_;
};

struct noise_transport {
    static constexpr size_t max_chunk_size = 65519;
    static constexpr size_t encrypted_header_size = message_header::size + 16;

    // max_receive_payload bounds the memory committed to an incoming frame.
    // Initial nonces allow testing exhaustion without iterating over 2^64
    // blocks. Never construct two active senders with the same key and nonce.
    noise_transport(noise_keys const& keys, bool initiator,
        uint32_t max_receive_payload = u24_max, uint64_t send_nonce = 0,
        uint64_t receive_nonce = 0);
    noise_transport(noise_transport const&) = delete;
    noise_transport& operator=(noise_transport const&) = delete;
    noise_transport(noise_transport&& other) noexcept;
    noise_transport& operator=(noise_transport&&) = delete;

    [[nodiscard]] std::optional<data_chunk> encrypt_frame(byte_span plaintext);
    // Incomplete input produces an empty list. An authentication, length or
    // nonce failure returns nullopt and permanently closes both directions.
    [[nodiscard]] std::optional<std::vector<data_chunk>> feed(byte_span ciphertext);

private:
    noise_detail::noise_cipher send_;
    noise_detail::noise_cipher receive_;
    uint32_t max_payload_;
    bool failed_ = false;
    bool reading_payload_ = false;
    size_t remaining_ = 0;
    data_chunk pending_;
    data_chunk frame_;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_NOISE_HPP
