// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sv2/noise.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <span>
#include <utility>

#include <secp256k1.h>
#include <secp256k1_ellswift.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/math/chacha20_poly1305.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/math/hkdf.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::node::sv2 {

namespace {

constexpr std::string_view protocol = "Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256";
constexpr uint64_t nonce_limit = std::numeric_limits<uint64_t>::max();

secp256k1_context const* context() {
    // A context is expensive to construct, and randomization mutates it. Each
    // thread owns one, then uses only const operations on it.
    static thread_local auto instance = [] {
        std::unique_ptr<secp256k1_context, decltype(&secp256k1_context_destroy)> value(
            secp256k1_context_create(SECP256K1_CONTEXT_NONE), &secp256k1_context_destroy);
        hash_digest seed;
        pseudo_random::fill_bytes(std::as_writable_bytes(std::span{seed}));
        auto const randomized = secp256k1_context_randomize(value.get(), seed.data());
        pseudo_random::wipe(seed);
        KTH_CONTRACT(randomized == 1);
        return value;
    }();
    return instance.get();
}

hash_digest xonly(secp256k1_pubkey const& public_key) {
    secp256k1_xonly_pubkey point;
    secp256k1_xonly_pubkey_from_pubkey(context(), &point, nullptr, &public_key);
    hash_digest result;
    secp256k1_xonly_pubkey_serialize(context(), result.data(), &point);
    return result;
}

hash_digest xonly(byte_array<64> const& encoding) {
    secp256k1_pubkey point;
    secp256k1_ellswift_decode(context(), &point, encoding.data());
    return xonly(point);
}

hash_digest exchange(noise_keypair const& ours, byte_array<64> const& theirs,
    bool initiator) {
    hash_digest shared;
    auto const& first = initiator ? ours.ellswift() : theirs;
    auto const& second = initiator ? theirs : ours.ellswift();
    auto const exchanged = secp256k1_ellswift_xdh(context(), shared.data(),
        first.data(), second.data(), ours.secret().data(), initiator ? 0 : 1,
        secp256k1_ellswift_xdh_hash_function_bip324, nullptr);
    // All encodings decode, and noise_keypair only holds validated secrets.
    KTH_CONTRACT(exchanged == 1);
    return shared;
}

chacha20_nonce nonce_bytes(uint64_t counter) {
    chacha20_nonce bytes{};
    for (size_t i = 0; i < sizeof(counter); ++i) {
        bytes[4 + i] = static_cast<uint8_t>(counter >> (8 * i));
    }
    return bytes;
}

uint32_t payload_size(byte_span header) {
    return uint32_t{header[3]} | (uint32_t{header[4]} << 8) | (uint32_t{header[5]} << 16);
}

hash_digest certificate_hash(noise_certificate const& certificate,
    hash_digest const& static_public) {
    auto const bytes = certificate.bytes();
    return sha256_hash(build_chunk({byte_span{bytes}.first(10), static_public}));
}

} // namespace

std::optional<noise_keypair> noise_keypair::create(hash_digest const& secret,
    hash_digest const& entropy) {
    noise_keypair result;
    if ( ! secp256k1_ellswift_create(context(), result.ellswift_.data(),
            secret.data(), entropy.data())) {
        return std::nullopt;
    }
    result.secret_ = secret;
    result.public_ = xonly(result.ellswift_);
    return result;
}

noise_keypair noise_keypair::generate() {
    hash_digest secret;
    hash_digest entropy;
    for (;;) {
        pseudo_random::fill_bytes(std::as_writable_bytes(std::span{secret}));
        pseudo_random::fill_bytes(std::as_writable_bytes(std::span{entropy}));
        auto result = create(secret, entropy);
        pseudo_random::wipe(secret);
        pseudo_random::wipe(entropy);
        if (result) return *result;
    }
}

noise_keypair::~noise_keypair() {
    pseudo_random::wipe(secret_);
}

std::optional<noise_certificate> noise_certificate::sign(
    hash_digest const& authority_secret, hash_digest const& static_public,
    uint32_t from, uint32_t until, hash_digest const& auxiliary) {
    secp256k1_keypair authority;
    if (from >= until || ! secp256k1_keypair_create(context(), &authority,
            authority_secret.data())) {
        return std::nullopt;
    }
    noise_certificate result{0, from, until, {}};
    auto const message = certificate_hash(result, static_public);
    auto const signed_message = secp256k1_schnorrsig_sign32(context(), result.signature.data(),
        message.data(), &authority, auxiliary.data());
    secp256k1_xonly_pubkey public_key;
    secp256k1_keypair_xonly_pub(context(), &public_key, nullptr, &authority);
    pseudo_random::wipe_bytes(std::as_writable_bytes(std::span{&authority, 1}));
    if ( ! signed_message || ! secp256k1_schnorrsig_verify(context(), result.signature.data(),
            message.data(), message.size(), &public_key)) {
        return std::nullopt;
    }
    return result;
}

bool noise_certificate::verify(hash_digest const& authority_public,
    hash_digest const& static_public, uint32_t now, uint32_t clock_leeway) const {
    if (version != 0 || valid_from >= not_valid_after) return false;
    // The window is widened at both ends by the leeway, without wrapping.
    auto const earliest = valid_from - std::min(valid_from, clock_leeway);
    auto const latest = not_valid_after + std::min(clock_leeway,
        std::numeric_limits<uint32_t>::max() - not_valid_after);
    if (now < earliest || now > latest) return false;
    secp256k1_xonly_pubkey authority;
    if ( ! secp256k1_xonly_pubkey_parse(context(), &authority, authority_public.data())) {
        return false;
    }
    auto const message = certificate_hash(*this, static_public);
    return secp256k1_schnorrsig_verify(context(), signature.data(), message.data(),
        message.size(), &authority) == 1;
}

byte_array<noise_certificate::size> noise_certificate::bytes() const {
    byte_array<size> result{};
    result[0] = static_cast<uint8_t>(version);
    result[1] = static_cast<uint8_t>(version >> 8);
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        result[2 + i] = static_cast<uint8_t>(valid_from >> (8 * i));
        result[6 + i] = static_cast<uint8_t>(not_valid_after >> (8 * i));
    }
    std::copy(signature.begin(), signature.end(), result.begin() + 10);
    return result;
}

std::optional<noise_certificate> noise_certificate::parse(byte_span bytes) {
    if (bytes.size() != size) return std::nullopt;
    noise_certificate result;
    result.version = uint16_t{bytes[0]} | (uint16_t{bytes[1]} << 8);
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        result.valid_from |= uint32_t{bytes[2 + i]} << (8 * i);
        result.not_valid_after |= uint32_t{bytes[6 + i]} << (8 * i);
    }
    std::copy(bytes.begin() + 10, bytes.end(), result.signature.begin());
    return result;
}

std::string noise_authority_key(hash_digest const& public_key) {
    data_chunk bytes{1, 0};
    bytes.insert(bytes.end(), public_key.begin(), public_key.end());
    auto const checksum = bitcoin_hash(bytes);
    bytes.insert(bytes.end(), checksum.begin(), checksum.begin() + 4);
    return encode_base58(bytes);
}

std::optional<hash_digest> noise_parse_authority_key(std::string_view text) {
    auto const decoded = decode_base58<38>(text);
    if ( ! decoded || (*decoded)[0] != 1 || (*decoded)[1] != 0) return std::nullopt;
    auto const checksum = bitcoin_hash(byte_span{*decoded}.first(34));
    if ( ! std::equal(checksum.begin(), checksum.begin() + 4, decoded->begin() + 34)) {
        return std::nullopt;
    }
    hash_digest result;
    std::copy_n(decoded->begin() + 2, result.size(), result.begin());
    secp256k1_xonly_pubkey point;
    if ( ! secp256k1_xonly_pubkey_parse(context(), &point, result.data())) return std::nullopt;
    return result;
}

noise_keys::~noise_keys() {
    pseudo_random::wipe(initiator_to_responder);
    pseudo_random::wipe(responder_to_initiator);
}

namespace noise_detail {

noise_cipher::~noise_cipher() {
    pseudo_random::wipe(key);
}

std::optional<data_chunk> noise_cipher::encrypt(byte_span ad, byte_span plaintext) {
    // Noise reserves UINT64_MAX for rekeying. It must never encrypt a message.
    if (nonce == nonce_limit) return std::nullopt;
    return chacha20_poly1305_encrypt(key, nonce_bytes(nonce++), ad, plaintext);
}

std::optional<data_chunk> noise_cipher::decrypt(byte_span ad, byte_span ciphertext) {
    if (nonce == nonce_limit) return std::nullopt;
    auto result = chacha20_poly1305_decrypt(key, nonce_bytes(nonce), ad, ciphertext);
    if (result) ++nonce;
    return result;
}

noise_symmetric_state::noise_symmetric_state() {
    auto const name = byte_span{reinterpret_cast<uint8_t const*>(protocol.data()), protocol.size()};
    chaining_key = sha256_hash(name);
    // MixHash of the empty prologue.
    hash = sha256_hash(chaining_key);
}

noise_symmetric_state::~noise_symmetric_state() {
    pseudo_random::wipe(chaining_key);
}

void noise_symmetric_state::mix_hash(byte_span bytes) {
    hash = sha256_hash(build_chunk({hash, bytes}));
}

void noise_symmetric_state::mix_key(byte_span bytes) {
    auto keys = hkdf_sha256_pair(chaining_key, bytes);
    chaining_key = keys.first;
    cipher.key = keys.second;
    cipher.nonce = 0;
    pseudo_random::wipe(keys.first);
    pseudo_random::wipe(keys.second);
}

std::optional<data_chunk> noise_symmetric_state::encrypt(byte_span plaintext) {
    auto result = cipher.encrypt(hash, plaintext);
    if (result) mix_hash(*result);
    return result;
}

std::optional<data_chunk> noise_symmetric_state::decrypt(byte_span ciphertext) {
    auto result = cipher.decrypt(hash, ciphertext);
    if (result) mix_hash(ciphertext);
    return result;
}

noise_keys noise_symmetric_state::split() const {
    auto keys = hkdf_sha256_pair(chaining_key, {});
    noise_keys result{keys.first, keys.second};
    pseudo_random::wipe(keys.first);
    pseudo_random::wipe(keys.second);
    return result;
}

} // namespace noise_detail

noise_initiator::noise_initiator(noise_keypair ephemeral,
    hash_digest authority_public, uint32_t now)
    : ephemeral_(std::move(ephemeral))
    , authority_(authority_public)
    , now_(now)
{}

std::optional<data_chunk> noise_initiator::start() {
    if (started_ || finished_) {
        finished_ = true;
        keys_.reset();
        return std::nullopt;
    }
    started_ = true;
    state_.mix_hash(ephemeral_.ellswift());
    state_.mix_hash({});
    return data_chunk(ephemeral_.ellswift().begin(), ephemeral_.ellswift().end());
}

bool noise_initiator::finish(byte_span response) {
    auto const ready = started_ && ! finished_;
    finished_ = true;
    keys_.reset();
    if ( ! ready || response.size() != noise_responder::flight_size) return false;

    byte_array<64> remote_ephemeral;
    std::copy_n(response.begin(), remote_ephemeral.size(), remote_ephemeral.begin());
    state_.mix_hash(remote_ephemeral);
    auto shared = exchange(ephemeral_, remote_ephemeral, true);
    state_.mix_key(shared);
    pseudo_random::wipe(shared);
    auto const static_bytes = state_.decrypt(response.subspan(64, 80));
    if ( ! static_bytes) return false;
    byte_array<64> remote_static;
    std::copy(static_bytes->begin(), static_bytes->end(), remote_static.begin());
    shared = exchange(ephemeral_, remote_static, true);
    state_.mix_key(shared);
    pseudo_random::wipe(shared);
    auto const certificate_bytes = state_.decrypt(response.subspan(144));
    if ( ! certificate_bytes) return false;
    auto const certificate = noise_certificate::parse(*certificate_bytes);
    // A server may sign a certificate that is valid from its own current
    // second, and two clocks never agree to the second: a client half a second
    // behind would refuse about half of such handshakes.
    if ( ! certificate || ! certificate->verify(authority_, xonly(remote_static), now_,
            certificate_clock_leeway_seconds)) {
        return false;
    }
    keys_ = state_.split();
    return true;
}

noise_responder::noise_responder(noise_keypair ephemeral, noise_keypair static_key,
    noise_certificate certificate)
    : ephemeral_(std::move(ephemeral))
    , static_(std::move(static_key))
    , certificate_(certificate)
{}

std::optional<data_chunk> noise_responder::respond(byte_span request) {
    auto const ready = ! used_;
    used_ = true;
    keys_.reset();
    if ( ! ready || request.size() != noise_initiator::flight_size) return std::nullopt;

    byte_array<64> remote;
    std::copy(request.begin(), request.end(), remote.begin());
    state_.mix_hash(remote);
    state_.mix_hash({});
    state_.mix_hash(ephemeral_.ellswift());
    auto shared = exchange(ephemeral_, remote, false);
    state_.mix_key(shared);
    pseudo_random::wipe(shared);
    auto const encrypted_static = state_.encrypt(static_.ellswift());
    if ( ! encrypted_static) return std::nullopt;
    shared = exchange(static_, remote, false);
    state_.mix_key(shared);
    pseudo_random::wipe(shared);
    auto const encrypted_certificate = state_.encrypt(certificate_.bytes());
    if ( ! encrypted_certificate) return std::nullopt;
    auto response = build_chunk({ephemeral_.ellswift(), *encrypted_static, *encrypted_certificate});
    keys_ = state_.split();
    return response;
}

noise_transport::noise_transport(noise_keys const& keys, bool initiator,
    uint32_t max_receive_payload, uint64_t send_nonce, uint64_t receive_nonce)
    : send_{initiator ? keys.initiator_to_responder : keys.responder_to_initiator, send_nonce}
    , receive_{initiator ? keys.responder_to_initiator : keys.initiator_to_responder, receive_nonce}
    , max_payload_(std::min(max_receive_payload, u24_max))
{}

noise_transport::noise_transport(noise_transport&& other) noexcept
    : send_(other.send_)
    , receive_(other.receive_)
    , max_payload_(other.max_payload_)
    , failed_(std::exchange(other.failed_, true))
    , reading_payload_(other.reading_payload_)
    , remaining_(other.remaining_)
    , pending_(std::move(other.pending_))
    , frame_(std::move(other.frame_))
{
    pseudo_random::wipe(other.send_.key);
    pseudo_random::wipe(other.receive_.key);
}

std::optional<data_chunk> noise_transport::encrypt_frame(byte_span plaintext) {
    if (failed_) return std::nullopt;
    if (plaintext.size() < message_header::size
        || payload_size(plaintext) != plaintext.size() - message_header::size) {
        failed_ = true;
        return std::nullopt;
    }
    auto const payload = plaintext.subspan(message_header::size);
    auto const blocks = 1 + (payload.size() + max_chunk_size - 1) / max_chunk_size;
    if (blocks > nonce_limit - send_.nonce) {
        failed_ = true;
        return std::nullopt;
    }
    auto bytes = send_.encrypt({}, plaintext.first(message_header::size));
    // The preflight check guarantees every nonce for this frame is available.
    KTH_CONTRACT(bytes.has_value());
    for (size_t offset = 0; offset < payload.size(); offset += max_chunk_size) {
        auto const size = std::min(max_chunk_size, payload.size() - offset);
        auto const encrypted = send_.encrypt({}, payload.subspan(offset, size));
        KTH_CONTRACT(encrypted.has_value());
        bytes->insert(bytes->end(), encrypted->begin(), encrypted->end());
    }
    return bytes;
}

std::optional<std::vector<data_chunk>> noise_transport::feed(byte_span ciphertext) {
    if (failed_) return std::nullopt;
    std::vector<data_chunk> frames;
    while ( ! ciphertext.empty()) {
        auto const needed = reading_payload_
            ? std::min(remaining_, max_chunk_size) + poly1305_tag_size
            : encrypted_header_size;
        auto const take = std::min(needed - pending_.size(), ciphertext.size());
        pending_.insert(pending_.end(), ciphertext.begin(), ciphertext.begin() + take);
        ciphertext = ciphertext.subspan(take);
        if (pending_.size() < needed) break;

        auto plaintext = receive_.decrypt({}, pending_);
        pending_.clear();
        if ( ! plaintext) {
            failed_ = true;
            return std::nullopt;
        }
        if ( ! reading_payload_) {
            remaining_ = payload_size(*plaintext);
            if (remaining_ > max_payload_) {
                failed_ = true;
                return std::nullopt;
            }
            frame_ = std::move(*plaintext);
            reading_payload_ = remaining_ != 0;
        } else {
            remaining_ -= plaintext->size();
            frame_.insert(frame_.end(), plaintext->begin(), plaintext->end());
            reading_payload_ = remaining_ != 0;
        }
        // An empty SV2 payload has no Noise payload block or tag.
        if ( ! reading_payload_) {
            frames.push_back(std::move(frame_));
            frame_.clear();
        }
    }
    return frames;
}

} // namespace kth::node::sv2
