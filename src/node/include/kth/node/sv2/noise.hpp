// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_NOISE_HPP
#define KTH_NODE_SV2_NOISE_HPP

#include <kth/node/sv2/feature.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/sv2/framing.hpp>

// The Noise transport of Stratum V2 (sv2-spec 4): the NX handshake over
// secp256k1 with EllSwift encodings, the certificate a server shows its
// clients, and the encrypted frames that follow. Nothing here touches a
// socket; the Template Provider moves the bytes.

namespace kth::node::sv2 {

// Why a key, a certificate, a handshake or a frame was refused, for the
// caller's log.
enum class noise_error {
    invalid_secret_key,         // zero, or not below the curve order
    invalid_public_key,         // not the x coordinate of a point on the curve
    invalid_authority_key,      // not the sv2-spec 4.7 encoding of a public key
    invalid_validity,           // a certificate whose validity window is empty
    malformed_certificate,      // not the 74 bytes of a certificate
    certificate_version,        // a version other than 0
    certificate_out_of_date,    // the clock is outside the validity window
    certificate_signature,      // not the authority's signature for this static key
    flight_size,                // a handshake message of the wrong length
    handshake_used,             // a handshake object used again, or out of order
    authentication,             // a tag that does not authenticate its bytes
    malformed_frame,            // a frame whose header does not give its payload's length
    frame_too_large,            // a received frame over the session's bound
    nonce_exhausted,            // no nonce left in that direction
    closed,                     // a transport that has failed before
    clock_out_of_range,         // a clock outside the certificate's U32 seconds
};

[[nodiscard]] std::string_view to_string(noise_error error);

// The EllSwift encoding is part of the transcript and of the ECDH input. The
// actual encoding is kept, never one regenerated from the same point.
struct noise_keypair {
    [[nodiscard]] static std::expected<noise_keypair, noise_error> create(
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
// seconds, and a server may sign from its current second.
inline constexpr uint32_t certificate_clock_leeway_seconds = 10;

struct noise_certificate {
    static constexpr size_t size = 74;

    uint16_t version = 0;
    uint32_t valid_from = 0;
    uint32_t not_valid_after = 0;
    byte_array<64> signature{};

    [[nodiscard]] static std::expected<noise_certificate, noise_error> sign(
        hash_digest const& authority_secret, hash_digest const& static_public,
        uint32_t from, uint32_t until, hash_digest const& auxiliary = {});
    // `now` may be outside the validity window by `clock_leeway` seconds. A
    // server checks its own certificate against its own clock, with none.
    [[nodiscard]] std::expected<void, noise_error> verify(hash_digest const& authority_public,
        hash_digest const& static_public, uint32_t now, uint32_t clock_leeway = 0) const;
    [[nodiscard]] byte_array<size> bytes() const;
    [[nodiscard]] static std::expected<noise_certificate, noise_error> parse(byte_span bytes);
};

// The prefix versions the authority encoding, not the certificate (sv2-spec 4.7).
[[nodiscard]] std::string noise_authority_key(hash_digest const& public_key);
[[nodiscard]] std::expected<hash_digest, noise_error> noise_parse_authority_key(std::string_view text);

struct noise_keys {
    hash_digest initiator_to_responder{};
    hash_digest responder_to_initiator{};
    ~noise_keys();
};

struct noise_transport;

namespace noise_detail {

struct noise_cipher {
    hash_digest key{};
    uint64_t nonce = 0;
    ~noise_cipher();

    [[nodiscard]] std::expected<data_chunk, noise_error> encrypt(byte_span ad, byte_span plaintext);
    [[nodiscard]] std::expected<data_chunk, noise_error> decrypt(byte_span ad, byte_span ciphertext);
};

struct noise_symmetric_state {
    noise_symmetric_state();
    ~noise_symmetric_state();

    void mix_hash(byte_span bytes);
    void mix_key(byte_span bytes);
    [[nodiscard]] std::expected<data_chunk, noise_error> encrypt(byte_span plaintext);
    [[nodiscard]] std::expected<data_chunk, noise_error> decrypt(byte_span ciphertext);
    [[nodiscard]] noise_keys split() const;

    hash_digest chaining_key{};
    hash_digest hash{};
    noise_cipher cipher;
};

// A transport whose counters start at the given nonces, for the tests of
// nonce exhaustion, which counting cannot reach. Never two senders with the
// same key and nonce.
[[nodiscard]] noise_transport transport_at(noise_keys const& keys, bool initiator,
    uint32_t max_receive_payload, uint64_t send_nonce, uint64_t receive_nonce);

} // namespace noise_detail

// Each object performs one handshake. Failure consumes it, and keys become
// available only after the complete flight has been authenticated.
struct noise_initiator {
    static constexpr size_t flight_size = 64;

    noise_initiator(noise_keypair ephemeral, hash_digest authority_public, uint32_t now);
    noise_initiator(noise_initiator const&) = delete;
    noise_initiator& operator=(noise_initiator const&) = delete;
    [[nodiscard]] std::expected<data_chunk, noise_error> start();
    [[nodiscard]] std::expected<void, noise_error> finish(byte_span response);
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
    [[nodiscard]] std::expected<data_chunk, noise_error> respond(byte_span request);
    [[nodiscard]] std::optional<noise_keys> const& keys() const { return keys_; }

private:
    noise_keypair ephemeral_;
    noise_keypair static_;
    noise_certificate certificate_;
    bool used_ = false;
    noise_detail::noise_symmetric_state state_;
    std::optional<noise_keys> keys_;
};

// What a read gave: the frames that authenticated, in order, and whether the
// transport is still open. Frames that authenticate before a failure come
// with it, so what a peer gets answered does not depend on how its bytes were
// split into reads.
struct noise_received {
    std::vector<data_chunk> frames;
    std::expected<void, noise_error> status;
};

struct noise_transport {
    static constexpr size_t max_chunk_size = 65519;
    static constexpr size_t encrypted_header_size = message_header::size + 16;

    // `max_receive_payload` bounds the memory one frame header can commit: a
    // header announcing more fails the transport before its payload is read.
    noise_transport(noise_keys const& keys, bool initiator, uint32_t max_receive_payload);
    noise_transport(noise_transport const&) = delete;
    noise_transport& operator=(noise_transport const&) = delete;
    noise_transport(noise_transport&& other) noexcept;
    noise_transport& operator=(noise_transport&&) = delete;

    // A failure closes that direction for good and wipes its key at once.
    // The other direction stays open, so that the frames that authenticated
    // before a failure can still be answered; the caller then closes the
    // connection.
    [[nodiscard]] std::expected<data_chunk, noise_error> encrypt_frame(byte_span plaintext);
    // Incomplete input is kept for the next call.
    [[nodiscard]] noise_received feed(byte_span ciphertext);

private:
    friend noise_transport noise_detail::transport_at(noise_keys const& keys, bool initiator,
        uint32_t max_receive_payload, uint64_t send_nonce, uint64_t receive_nonce);

    noise_error fail_send(noise_error error);
    noise_error fail_receive(noise_error error);

    noise_detail::noise_cipher send_;
    noise_detail::noise_cipher receive_;
    uint32_t max_payload_;
    bool send_failed_ = false;
    bool receive_failed_ = false;
    bool reading_payload_ = false;
    size_t remaining_ = 0;
    data_chunk pending_;
    data_chunk frame_;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_NOISE_HPP
