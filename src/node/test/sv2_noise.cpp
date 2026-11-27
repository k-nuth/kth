// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/infrastructure.hpp>
#include <kth/infrastructure/math/chacha20_poly1305.hpp>
#include <kth/node/sv2/noise.hpp>

using namespace kth;
using namespace kth::node::sv2;

namespace {

constexpr uint32_t now = 1700000000;
constexpr uint32_t valid_from = 1699999900;
constexpr uint32_t valid_until = 1700003600;

template <size_t Size>
byte_array<Size> fixed(std::string_view text) {
    auto const value = decode_base16<Size>(text);
    REQUIRE(value);
    return *value;
}

hash_digest secret(uint8_t value) {
    hash_digest bytes{};
    bytes.back() = value;
    return bytes;
}

noise_keypair keypair(uint8_t value) {
    auto const key = noise_keypair::create(secret(value), {});
    REQUIRE(key);
    return *key;
}

noise_certificate certificate(uint8_t static_secret = 3,
    uint32_t from = valid_from, uint32_t until = valid_until) {
    auto const result = noise_certificate::sign(secret(4),
        keypair(static_secret).public_key(), from, until);
    REQUIRE(result);
    return *result;
}

noise_keys handshake() {
    noise_initiator initiator(keypair(1), keypair(4).public_key(), now);
    noise_responder responder(keypair(2), keypair(3), certificate());
    auto const first = initiator.start();
    REQUIRE(first);
    auto const second = responder.respond(*first);
    REQUIRE(second);
    REQUIRE(initiator.finish(*second));
    REQUIRE(initiator.keys());
    REQUIRE(responder.keys());
    CHECK(initiator.keys()->initiator_to_responder == responder.keys()->initiator_to_responder);
    CHECK(initiator.keys()->responder_to_initiator == responder.keys()->responder_to_initiator);
    return *initiator.keys();
}

data_chunk frame(size_t size, uint8_t type = 0x71) {
    auto bytes = to_data_chunk(message_header{0x8001, type, uint32_t(size)});
    for (size_t i = 0; i < size; ++i) bytes.push_back(uint8_t(i));
    return bytes;
}

chacha20_nonce nonce(uint64_t value) {
    chacha20_nonce result{};
    for (size_t i = 0; i < sizeof(value); ++i) {
        result[4 + i] = uint8_t(value >> (8 * i));
    }
    return result;
}

std::string checked_base58(data_chunk bytes) {
    auto const checksum = bitcoin_hash(bytes);
    bytes.insert(bytes.end(), checksum.begin(), checksum.begin() + 4);
    return encode_base58(bytes);
}

// A direction that failed stays closed; each fails on its own.
void require_receive_closed(noise_transport& transport) {
    auto const received = transport.feed({});
    REQUIRE_FALSE(received.status);
    CHECK(received.status.error() == noise_error::closed);
    CHECK(received.frames.empty());
}

void require_send_closed(noise_transport& transport, byte_span valid_frame) {
    auto const sealed = transport.encrypt_frame(valid_frame);
    REQUIRE_FALSE(sealed);
    CHECK(sealed.error() == noise_error::closed);
}

// The frames of every call, until one fails.
noise_received feed_all(noise_transport& receiver, byte_span wire, std::vector<size_t> const& cuts) {
    noise_received all;
    size_t offset = 0;
    for (size_t cut : cuts) {
        auto part = receiver.feed(wire.subspan(offset, cut - offset));
        all.frames.insert(all.frames.end(), part.frames.begin(), part.frames.end());
        if ( ! part.status) {
            all.status = part.status;
            return all;
        }
        offset = cut;
    }
    auto last = receiver.feed(wire.subspan(offset));
    all.frames.insert(all.frames.end(), last.frames.begin(), last.frames.end());
    all.status = last.status;
    return all;
}

} // namespace

TEST_CASE("sv2 noise fixed handshake matches its derived transcript", "[sv2 noise]") {
    // Derived fixture, not an SV2 published vector: secrets 1, 2, 3, 4 and
    // zero EllSwift and BIP340 auxiliary entropy. Independently computed from
    // sv2-spec 4 using the vendored libsecp256k1 0.8.0 EllSwift algorithm,
    // field arithmetic, SHA256, HKDF and RFC 8439 AEAD.
    // The derivation's primitives were checked against BIP324's inverse
    // vector, BIP340 vector 0 and RFC 8439 2.8.2.
    auto const cert = certificate();
    CHECK(encode_base16(cert.bytes()) ==
        "00009cf0536510ff5365a8381ae9a27553c93e03e77bae7526450e3a06685c3944b0220c2f813c526c5f"
        "d51805e0db453d1bd78f823d9647a18b9399933dd24362de35e69fb92b124d8e");
    noise_initiator initiator(keypair(1), keypair(4).public_key(), now);
    noise_responder responder(keypair(2), keypair(3), cert);
    REQUIRE_FALSE(initiator.keys());
    REQUIRE_FALSE(responder.keys());
    auto const first = initiator.start();
    REQUIRE(first);
    CHECK(first->size() == noise_initiator::flight_size);
    CHECK(encode_base16(*first) ==
        "3f1ffea5c7407cc506414c5132ca2669d0e38f045a3773f20a1a84b732e998d17"
        "886d626782ac56802494197fe7ef79782c18007cb6f984ec4f42b20b504fce2");
    auto const second = responder.respond(*first);
    REQUIRE(second);
    CHECK(second->size() == noise_responder::flight_size);
    CHECK(encode_base16(*second) ==
        "032e60917768a3f77bdaa25c3f291bf425b1416d67b22da48021bfcd508be8d6a"
        "8c3a5d674d012582483ee25ef47e891ac0f7374ba0a4fb006ae198175aa3b00"
        "6d76bf70a68c247a586268e94b181fa020a89f1ffa6139050ec39fcc4e7e3248e"
        "82fc30ad6a0cb70ea111791c2ca087a8a4060ca0eb7893d1dbce5dee591d0e1f"
        "3b0f079657737c51a01eb5da64d0bcfefdf0a49b22f83468288605f137fd6e630"
        "b29acfab4fda5899026ced46aa22dc68544c1dd89c1a698d65f83316fa78f1ed6"
        "3e6af58ef5f1d259e5186d9534e78dccddbb3c076b3e72b1b6fa3b2251c4e0"
        "a121557e301260074a2");
    REQUIRE(initiator.finish(*second));
    REQUIRE(initiator.keys());
    REQUIRE(responder.keys());
    CHECK(encode_base16(initiator.keys()->initiator_to_responder) ==
        "b353d8554e15e69e9ef7b7d24ecce7dcf569b169cd1f4f108e613ba39ae0804e");
    CHECK(encode_base16(initiator.keys()->responder_to_initiator) ==
        "2cfaf4454b56d19f9bd984af648a47417e9458bd3381905a1c498672f7c20c9b");
    CHECK(initiator.keys()->initiator_to_responder == responder.keys()->initiator_to_responder);
    CHECK(initiator.keys()->responder_to_initiator == responder.keys()->responder_to_initiator);
    auto const again = initiator.finish(*second);
    REQUIRE_FALSE(again);
    CHECK(again.error() == noise_error::handshake_used);
    CHECK_FALSE(initiator.start());
    CHECK_FALSE(responder.respond(*first));
}

TEST_CASE("sv2 noise transport frames match fixed ciphertext", "[sv2 noise]") {
    // Two frames from the initiator, then one back, under the keys of the
    // fixed handshake above. The second frame's header takes nonce 2 and its
    // 65535-byte payload nonces 3 and 4: a counter above 0 in the nonce, and
    // a payload split into a full block and a short one. The expected bytes
    // were computed with OpenSSL 3 (Python cryptography 46) from those keys
    // and the sv2-spec 4.5.3 framing, not with this implementation.
    auto const keys = handshake();
    auto sender = noise_transport(keys, true, u24_max);
    auto const first = sender.encrypt_frame(frame(17, 0x71));
    REQUIRE(first);
    CHECK(encode_base16(*first) ==
        "8cf978f6db22d1f4f9a3a19fd55658d4e84dcb4320a75e1105203297f31b4a74311c657ebb709c91df1c78"
        "99b3dd33da7cff3610e9591c");
    auto const second = sender.encrypt_frame(frame(65535, 0x72));
    REQUIRE(second);
    REQUIRE(second->size() == 65589);
    CHECK(encode_base16(byte_span(*second).first(22)) == "d18dbb8b97eef93de10993bbbf4dfa036e07c1ea2b3f");
    CHECK(encode_base16(sha256_hash(*second)) ==
        "d0e13bbb2811a5142cdc63e197e72a76f7bdbb2a5dfe08ca1d55e038e90dd37c");

    auto receiver = noise_transport(keys, false, u24_max);
    auto wire = *first;
    wire.insert(wire.end(), second->begin(), second->end());
    auto const received = receiver.feed(wire);
    REQUIRE(received.status);
    REQUIRE(received.frames.size() == 2);
    CHECK(received.frames[0] == frame(17, 0x71));
    CHECK(received.frames[1] == frame(65535, 0x72));

    auto const reply = receiver.encrypt_frame(frame(0, 0x73));
    REQUIRE(reply);
    CHECK(encode_base16(*reply) == "cdbf99e08e5e7a15208a16b2695ba6e3508a53ebe650");
    auto const opened = sender.feed(*reply);
    REQUIRE(opened.status);
    REQUIRE(opened.frames.size() == 1);
    CHECK(opened.frames.front() == frame(0, 0x73));
}

TEST_CASE("sv2 noise transport seals payloads near the largest block as fixed ciphertext", "[sv2 noise]") {
    // Each size alone, from nonce 0. Computed as in the case above.
    struct vector {
        size_t payload;
        size_t wire;
        char const* sha256;
    };
    vector const vectors[] = {
        {65518, 65556, "22359e05fc3b523c4218e631025a91ea8fa3730d5088bde5c1b7af2f27bbec11"},
        {65519, 65557, "117079a0a45107576fa543d1742e5bec872e3e689c2eb2257506bf120720bed0"},
        {65520, 65574, "1ba05b548187981fc1392ed8e3584b119edae756512c4f2b05f36ed48b99f7ac"},
        {65535, 65589, "155c5c5bb9970465ea09bf657f9b6a3b1d34a1e605d94d950dcbbde470187e5f"},
        {65536, 65590, "9759f90ddde53e5259a295ecf2330cfbe9282dfd4f56e38b58079c6517981751"},
    };
    auto const keys = handshake();
    for (auto const& v : vectors) {
        CAPTURE(v.payload);
        auto sender = noise_transport(keys, true, u24_max);
        auto const sealed = sender.encrypt_frame(frame(v.payload));
        REQUIRE(sealed);
        CHECK(sealed->size() == v.wire);
        CHECK(encode_base16(sha256_hash(*sealed)) == v.sha256);
        auto receiver = noise_transport(keys, false, u24_max);
        auto const received = receiver.feed(*sealed);
        REQUIRE(received.status);
        REQUIRE(received.frames.size() == 1);
        CHECK(received.frames.front() == frame(v.payload));
    }
}

TEST_CASE("sv2 noise authority encoding matches section 4.7.1 and is strict", "[sv2 noise]") {
    hash_digest const public_key{118, 99, 112, 0, 151, 156, 28, 17,
        175, 12, 48, 11, 205, 140, 127, 228, 134, 16, 252, 233,
        185, 193, 30, 61, 174, 227, 90, 224, 176, 138, 116, 85};
    std::string const expected = "9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh";
    CHECK(noise_authority_key(public_key) == expected);
    auto const parsed = noise_parse_authority_key(expected);
    REQUIRE(parsed);
    CHECK(*parsed == public_key);
    for (auto const& invalid : {std::string{}, std::string{"0OIl"}, expected + "1",
                                  std::string{" "} + expected, expected + " ",
                                  expected.substr(0, expected.size() - 1) + "1"}) {
        CAPTURE(invalid);
        auto const refused = noise_parse_authority_key(invalid);
        REQUIRE_FALSE(refused);
        CHECK(refused.error() == noise_error::invalid_authority_key);
    }
    auto encoded = data_chunk{1, 0};
    encoded.insert(encoded.end(), public_key.begin(), public_key.end());
    encoded[0] = 2;
    CHECK_FALSE(noise_parse_authority_key(checked_base58(encoded)));
    encoded[0] = 1;
    encoded[1] = 1;
    CHECK_FALSE(noise_parse_authority_key(checked_base58(encoded)));
    encoded = {1, 0};
    encoded.resize(34, 0xff);
    CHECK_FALSE(noise_parse_authority_key(checked_base58(encoded)));
    encoded = {1, 0};
    CHECK_FALSE(noise_parse_authority_key(checked_base58(encoded)));
}

TEST_CASE("sv2 noise keys and certificates reject invalid inputs", "[sv2 noise]") {
    hash_digest invalid{};
    auto const zero = noise_keypair::create(invalid, {});
    REQUIRE_FALSE(zero);
    CHECK(zero.error() == noise_error::invalid_secret_key);
    invalid.fill(0xff);
    CHECK_FALSE(noise_keypair::create(invalid, {}));
    CHECK_FALSE(noise_keypair::create(fixed<32>(
        "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141"), {}));
    auto const generated = noise_keypair::generate();
    auto const reconstructed = noise_keypair::create(generated.secret(), {});
    REQUIRE(reconstructed);
    CHECK(reconstructed->public_key() == generated.public_key());

    auto const authority = keypair(4).public_key();
    auto const server = keypair(3).public_key();
    auto cert = certificate();
    CHECK(cert.verify(authority, server, valid_from));
    CHECK(cert.verify(authority, server, valid_until));
    auto const early_clock = cert.verify(authority, server, valid_from - 1);
    REQUIRE_FALSE(early_clock);
    CHECK(early_clock.error() == noise_error::certificate_out_of_date);
    CHECK_FALSE(cert.verify(authority, server, valid_until + 1));
    // With a leeway, the verifier's clock may be that far behind or ahead of
    // the signer's, and no further.
    auto const leeway = certificate_clock_leeway_seconds;
    CHECK(cert.verify(authority, server, valid_from - 1, leeway));
    CHECK(cert.verify(authority, server, valid_from - leeway, leeway));
    CHECK_FALSE(cert.verify(authority, server, valid_from - leeway - 1, leeway));
    CHECK(cert.verify(authority, server, valid_until + 1, leeway));
    CHECK(cert.verify(authority, server, valid_until + leeway, leeway));
    CHECK_FALSE(cert.verify(authority, server, valid_until + leeway + 1, leeway));
    // The widened window stops at the ends of the timestamp range.
    auto const last = std::numeric_limits<uint32_t>::max();
    auto const early = noise_certificate::sign(keypair(4).secret(), server, 3, 100);
    REQUIRE(early);
    CHECK(early->verify(authority, server, 0, leeway));
    CHECK_FALSE(early->verify(authority, server, last, leeway));
    auto const late = noise_certificate::sign(keypair(4).secret(), server, last - 100, last - 3);
    REQUIRE(late);
    CHECK(late->verify(authority, server, last, leeway));
    CHECK_FALSE(late->verify(authority, server, 0, leeway));
    auto const other_authority = cert.verify(keypair(5).public_key(), server, now);
    REQUIRE_FALSE(other_authority);
    CHECK(other_authority.error() == noise_error::certificate_signature);
    CHECK_FALSE(cert.verify(authority, keypair(6).public_key(), now));
    auto const off_curve = cert.verify(invalid, server, now);
    REQUIRE_FALSE(off_curve);
    CHECK(off_curve.error() == noise_error::invalid_public_key);
    auto const no_secret = noise_certificate::sign({}, server, valid_from, valid_until);
    REQUIRE_FALSE(no_secret);
    CHECK(no_secret.error() == noise_error::invalid_secret_key);
    auto const backwards = noise_certificate::sign(secret(4), server, valid_until, valid_from);
    REQUIRE_FALSE(backwards);
    CHECK(backwards.error() == noise_error::invalid_validity);
    cert.signature[0] ^= 1;
    CHECK_FALSE(cert.verify(authority, server, now));
    cert = certificate();
    cert.version = 1;
    auto const version = cert.verify(authority, server, now);
    REQUIRE_FALSE(version);
    CHECK(version.error() == noise_error::certificate_version);
    cert = certificate();
    cert.valid_from = valid_until;
    cert.not_valid_after = valid_from;
    CHECK_FALSE(cert.verify(authority, server, now));

    auto const bytes = certificate().bytes();
    auto const parsed = noise_certificate::parse(bytes);
    REQUIRE(parsed);
    CHECK(parsed->bytes() == bytes);
    auto const short_bytes = noise_certificate::parse(byte_span(bytes).first(bytes.size() - 1));
    REQUIRE_FALSE(short_bytes);
    CHECK(short_bytes.error() == noise_error::malformed_certificate);
    data_chunk too_long(bytes.begin(), bytes.end());
    too_long.push_back(0);
    CHECK_FALSE(noise_certificate::parse(too_long));
}

TEST_CASE("sv2 noise authenticates the first flight and both second flight tags", "[sv2 noise]") {
    // The first flight is a bare EllSwift encoding. Its integrity is checked
    // by the second flight's tag and transcript, rather than a tag of its own.
    for (size_t const position : {size_t{0}, size_t{63}}) {
        noise_initiator initiator(keypair(1), keypair(4).public_key(), now);
        noise_responder responder(keypair(2), keypair(3), certificate());
        auto first = initiator.start();
        REQUIRE(first);
        (*first)[position] ^= 1;
        auto const second = responder.respond(*first);
        REQUIRE(second);
        auto const finished = initiator.finish(*second);
        REQUIRE_FALSE(finished);
        CHECK(finished.error() == noise_error::authentication);
        CHECK_FALSE(initiator.keys());
        CHECK_FALSE(initiator.finish(*second));
    }
    for (size_t const position : {size_t{0}, size_t{63}, size_t{64}, size_t{128},
                                  size_t{143}, size_t{144}, size_t{218}, size_t{233}}) {
        CAPTURE(position);
        noise_initiator initiator(keypair(1), keypair(4).public_key(), now);
        noise_responder responder(keypair(2), keypair(3), certificate());
        auto const first = initiator.start();
        REQUIRE(first);
        auto second = responder.respond(*first);
        REQUIRE(second);
        (*second)[position] ^= 1;
        CHECK_FALSE(initiator.finish(*second));
        CHECK_FALSE(initiator.keys());
    }
}

TEST_CASE("sv2 noise initiator refuses unauthenticated or out of date certificates", "[sv2 noise]") {
    for (auto const authority : {keypair(4).public_key(), keypair(5).public_key()}) {
        // Outside the window by more than the leeway a clock may be off.
        for (auto const time : {valid_from - certificate_clock_leeway_seconds - 1, now,
                valid_until + certificate_clock_leeway_seconds + 1}) {
            if (authority == keypair(4).public_key() && time == now) continue;
            noise_initiator initiator(keypair(1), authority, time);
            noise_responder responder(keypair(2), keypair(3), certificate());
            auto const first = initiator.start();
            REQUIRE(first);
            auto const second = responder.respond(*first);
            REQUIRE(second);
            CHECK_FALSE(initiator.finish(*second));
            CHECK_FALSE(initiator.keys());
        }
    }
    // A client whose clock is a little behind the signer's, or a little ahead of
    // the end of the window, still completes the handshake.
    for (auto const time : {valid_from - certificate_clock_leeway_seconds,
            valid_until + certificate_clock_leeway_seconds}) {
        noise_initiator initiator(keypair(1), keypair(4).public_key(), time);
        noise_responder responder(keypair(2), keypair(3), certificate());
        auto const first = initiator.start();
        REQUIRE(first);
        auto const second = responder.respond(*first);
        REQUIRE(second);
        CHECK(initiator.finish(*second));
        CHECK(initiator.keys());
    }
    for (int const defect : {0, 1, 2}) {
        auto cert = certificate(defect == 0 ? 6 : 3);
        if (defect == 1) cert.signature.back() ^= 1;
        if (defect == 2) cert.version = 1;
        noise_initiator initiator(keypair(1), keypair(4).public_key(), now);
        noise_responder responder(keypair(2), keypair(3), cert);
        auto const first = initiator.start();
        REQUIRE(first);
        auto const second = responder.respond(*first);
        REQUIRE(second);
        auto const finished = initiator.finish(*second);
        REQUIRE_FALSE(finished);
        CHECK(finished.error() == (defect == 2 ? noise_error::certificate_version
                                               : noise_error::certificate_signature));
        CHECK_FALSE(initiator.keys());
    }
}

TEST_CASE("sv2 noise handshakes are single use and require exact flight lengths", "[sv2 noise]") {
    for (size_t const size : {size_t{0}, size_t{63}, size_t{65}}) {
        noise_responder responder(keypair(2), keypair(3), certificate());
        auto const refused = responder.respond(data_chunk(size));
        REQUIRE_FALSE(refused);
        CHECK(refused.error() == noise_error::flight_size);
        CHECK_FALSE(responder.keys());
        auto const again = responder.respond(keypair(1).ellswift());
        REQUIRE_FALSE(again);
        CHECK(again.error() == noise_error::handshake_used);
    }
    for (size_t const size : {size_t{0}, size_t{233}, size_t{235}}) {
        noise_initiator initiator(keypair(1), keypair(4).public_key(), now);
        REQUIRE(initiator.start());
        auto const refused = initiator.finish(data_chunk(size));
        REQUIRE_FALSE(refused);
        CHECK(refused.error() == noise_error::flight_size);
        CHECK_FALSE(initiator.keys());
        CHECK_FALSE(initiator.finish(data_chunk(234)));
    }
    noise_initiator unstarted(keypair(1), keypair(4).public_key(), now);
    CHECK_FALSE(unstarted.finish(data_chunk(234)));
    CHECK_FALSE(unstarted.start());
    noise_initiator repeated(keypair(1), keypair(4).public_key(), now);
    REQUIRE(repeated.start());
    CHECK_FALSE(repeated.start());
}

TEST_CASE("sv2 noise transport seals headers separately and uses fixed payload chunks", "[sv2 noise]") {
    auto const keys = handshake();
    for (size_t const size : {size_t{0}, size_t{1}, size_t{65519}, size_t{65520}}) {
        CAPTURE(size);
        auto const plain = frame(size);
        noise_transport sender(keys, true, u24_max);
        noise_transport receiver(keys, false, u24_max);
        auto const encrypted = sender.encrypt_frame(plain);
        REQUIRE(encrypted);
        CHECK(encrypted->size() == message_header::size + 16 + size
            + 16 * ((size + 65518) / 65519));
        auto const decrypted = receiver.feed(*encrypted);
        REQUIRE(decrypted.status);
        REQUIRE(decrypted.frames.size() == 1);
        CHECK(decrypted.frames.front() == plain);

        // The reverse direction has an independent key and nonce sequence.
        auto const reply = receiver.encrypt_frame(plain);
        REQUIRE(reply);
        CHECK(*reply != *encrypted);
        auto const opened = sender.feed(*reply);
        REQUIRE(opened.status);
        REQUIRE(opened.frames.size() == 1);
        CHECK(opened.frames.front() == plain);
    }
}

TEST_CASE("sv2 noise transport decrypts fragmented and coalesced frames", "[sv2 noise]") {
    auto const keys = handshake();
    noise_transport sender(keys, true, u24_max);
    noise_transport receiver(keys, false, u24_max);
    std::vector<data_chunk> const frames{frame(0), frame(17), frame(65520), frame(3)};
    data_chunk wire;
    for (auto const& plain : frames) {
        auto const sealed = sender.encrypt_frame(plain);
        REQUIRE(sealed);
        wire.insert(wire.end(), sealed->begin(), sealed->end());
    }
    REQUIRE(receiver.feed({}).status);
    // Split both tags and the boundary between the full and final payload
    // chunks. No plaintext is released until the whole frame authenticates.
    auto const received = feed_all(receiver, wire, {1, 21, 23, 59, 65614});
    REQUIRE(received.status);
    CHECK(received.frames == frames);

    noise_transport small_sender(keys, false, u24_max);
    noise_transport small_receiver(keys, true, u24_max);
    auto const small = frame(3);
    auto const encrypted = small_sender.encrypt_frame(small);
    REQUIRE(encrypted);
    for (size_t i = 0; i < encrypted->size(); ++i) {
        auto const result = small_receiver.feed(byte_span(*encrypted).subspan(i, 1));
        REQUIRE(result.status);
        if (i + 1 == encrypted->size()) {
            REQUIRE(result.frames.size() == 1);
            CHECK(result.frames.front() == small);
        } else {
            CHECK(result.frames.empty());
        }
    }
}

TEST_CASE("sv2 noise transport keeps a valid frame read with a bad one, however the reads split", "[sv2 noise]") {
    // A valid frame and a tampered one in the same bytes: the valid one is
    // delivered and then the failure, at every split of the bytes into two
    // reads and in one read. Before, a failure in the same read took the
    // frames already opened with it.
    auto const keys = handshake();
    noise_transport sender(keys, true, u24_max);
    auto const good = sender.encrypt_frame(frame(5));
    auto bad = sender.encrypt_frame(frame(9));
    REQUIRE(good);
    REQUIRE(bad);
    bad->back() ^= 1;
    auto wire = *good;
    wire.insert(wire.end(), bad->begin(), bad->end());
    for (size_t split = 0; split <= wire.size(); ++split) {
        CAPTURE(split);
        noise_transport receiver(keys, false, u24_max);
        auto const received = feed_all(receiver, wire,
            split == 0 || split == wire.size() ? std::vector<size_t>{} : std::vector<size_t>{split});
        REQUIRE(received.frames.size() == 1);
        CHECK(received.frames.front() == frame(5));
        REQUIRE_FALSE(received.status);
        CHECK(received.status.error() == noise_error::authentication);
        require_receive_closed(receiver);
        // The valid frame can still be answered.
        CHECK(receiver.encrypt_frame(frame(1)));
    }
}

TEST_CASE("sv2 noise transport refuses tampered tags even across reads", "[sv2 noise]") {
    auto const keys = handshake();
    auto const plain = frame(65520);
    noise_transport sender(keys, true, u24_max);
    auto const encrypted = sender.encrypt_frame(plain);
    REQUIRE(encrypted);
    for (size_t const position : {size_t{0}, size_t{6}, size_t{21}, size_t{22},
                                  size_t{65556}, encrypted->size() - 1}) {
        for (bool const split : {false, true}) {
            CAPTURE(position, split);
            auto changed = *encrypted;
            changed[position] ^= 1;
            noise_transport receiver(keys, false, u24_max);
            if (split) {
                auto const partial = receiver.feed(byte_span(changed).first(position));
                REQUIRE(partial.status);
                CHECK(partial.frames.empty());
                auto const rest = receiver.feed(byte_span(changed).subspan(position));
                REQUIRE_FALSE(rest.status);
                CHECK(rest.status.error() == noise_error::authentication);
            } else {
                CHECK_FALSE(receiver.feed(changed).status);
            }
            require_receive_closed(receiver);
            CHECK(receiver.encrypt_frame(plain));
        }
    }
    noise_transport wrong_direction(keys, true, u24_max);
    CHECK_FALSE(wrong_direction.feed(*encrypted).status);
    require_receive_closed(wrong_direction);
}

TEST_CASE("sv2 noise transport moves buffered input without reusing nonces", "[sv2 noise]") {
    auto const keys = handshake();
    auto const plain = frame(7);
    noise_transport sender(keys, true, u24_max);
    noise_transport receiver(keys, false, u24_max);
    auto const first = sender.encrypt_frame(plain);
    REQUIRE(first);
    auto const partial = receiver.feed(byte_span(*first).first(25));
    REQUIRE(partial.status);
    CHECK(partial.frames.empty());
    noise_transport moved_receiver(std::move(receiver));
    require_receive_closed(receiver);
    require_send_closed(receiver, plain);
    auto const opened = moved_receiver.feed(byte_span(*first).subspan(25));
    REQUIRE(opened.status);
    REQUIRE(opened.frames.size() == 1);
    CHECK(opened.frames.front() == plain);

    noise_transport moved_sender(std::move(sender));
    require_receive_closed(sender);
    require_send_closed(sender, plain);
    auto const second = moved_sender.encrypt_frame(plain);
    REQUIRE(second);
    CHECK(*second != *first);
    auto const next = moved_receiver.feed(*second);
    REQUIRE(next.status);
    REQUIRE(next.frames.size() == 1);
    CHECK(next.frames.front() == plain);
}

TEST_CASE("sv2 noise transport preserves payload bounds and exact frame lengths", "[sv2 noise]") {
    auto const keys = handshake();
    auto const valid = frame(3);
    for (auto const& invalid : {data_chunk{}, data_chunk(5), frame(2), frame(4)}) {
        auto changed = invalid;
        if (changed.size() >= message_header::size) changed[3] = 3;
        noise_transport sender(keys, true, u24_max);
        auto const refused = sender.encrypt_frame(changed);
        REQUIRE_FALSE(refused);
        CHECK(refused.error() == noise_error::malformed_frame);
        require_send_closed(sender, valid);
    }
    noise_transport sender(keys, true, u24_max);
    auto const encrypted = sender.encrypt_frame(valid);
    REQUIRE(encrypted);
    for (bool const split : {false, true}) {
        // A bound of two bytes refuses the three this header announces,
        // before any of the payload is read.
        noise_transport receiver(keys, false, 2);
        if (split) {
            auto const partial = receiver.feed(byte_span(*encrypted).first(21));
            REQUIRE(partial.status);
            CHECK(partial.frames.empty());
            auto const rest = receiver.feed(byte_span(*encrypted).subspan(21));
            REQUIRE_FALSE(rest.status);
            CHECK(rest.status.error() == noise_error::frame_too_large);
        } else {
            CHECK_FALSE(receiver.feed(*encrypted).status);
        }
        require_receive_closed(receiver);
    }

    // Valid tags do not permit a sender to choose shorter intermediate
    // chunks: their size is implied by the authenticated plaintext length.
    auto const large = frame(65520);
    auto wrong_chunks = chacha20_poly1305_encrypt(keys.initiator_to_responder,
        nonce(0), {}, byte_span(large).first(message_header::size));
    auto const short_chunk = chacha20_poly1305_encrypt(keys.initiator_to_responder,
        nonce(1), {}, byte_span(large).subspan(message_header::size, 1));
    auto const rest = chacha20_poly1305_encrypt(keys.initiator_to_responder,
        nonce(2), {}, byte_span(large).subspan(message_header::size + 1));
    wrong_chunks.insert(wrong_chunks.end(), short_chunk.begin(), short_chunk.end());
    wrong_chunks.insert(wrong_chunks.end(), rest.begin(), rest.end());
    noise_transport strict(keys, false, u24_max);
    auto const first_chunk = strict.feed(byte_span(wrong_chunks).first(39));
    REQUIRE(first_chunk.status);
    CHECK(first_chunk.frames.empty());
    CHECK_FALSE(strict.feed(byte_span(wrong_chunks).subspan(39)).status);
    require_receive_closed(strict);
}

TEST_CASE("sv2 noise transport refuses exhausted nonces without wrapping", "[sv2 noise]") {
    auto const keys = handshake();
    auto const empty = frame(0);
    auto const payload = frame(1);
    auto const limit = std::numeric_limits<uint64_t>::max();
    auto exhausted_sender = noise_detail::transport_at(keys, true, u24_max, limit, 0);
    auto const refused = exhausted_sender.encrypt_frame(empty);
    REQUIRE_FALSE(refused);
    CHECK(refused.error() == noise_error::nonce_exhausted);
    require_send_closed(exhausted_sender, empty);
    noise_transport sender(keys, true, u24_max);
    auto const wire = sender.encrypt_frame(empty);
    REQUIRE(wire);
    auto exhausted_receiver = noise_detail::transport_at(keys, false, u24_max, 0, limit);
    auto const unopened = exhausted_receiver.feed(*wire);
    REQUIRE_FALSE(unopened.status);
    CHECK(unopened.status.error() == noise_error::nonce_exhausted);
    require_receive_closed(exhausted_receiver);

    auto last_sender = noise_detail::transport_at(keys, true, u24_max, limit - 1, 0);
    auto last_receiver = noise_detail::transport_at(keys, false, u24_max, 0, limit - 1);
    auto const last = last_sender.encrypt_frame(empty);
    REQUIRE(last);
    auto const received = last_receiver.feed(*last);
    REQUIRE(received.status);
    REQUIRE(received.frames.size() == 1);
    CHECK(received.frames.front() == empty);
    CHECK_FALSE(last_sender.encrypt_frame(empty));
    CHECK_FALSE(last_receiver.feed(*last).status);

    // A header and payload need two nonces. Finishing the header at the
    // last legal nonce cannot permit its payload to wrap back to zero.
    auto partial_sender = noise_detail::transport_at(keys, true, u24_max, limit - 1, 0);
    CHECK_FALSE(partial_sender.encrypt_frame(payload));
    auto sealed_header = chacha20_poly1305_encrypt(keys.initiator_to_responder,
        nonce(limit - 1), {}, byte_span(payload).first(message_header::size));
    sealed_header.resize(sealed_header.size() + 17);
    auto partial_receiver = noise_detail::transport_at(keys, false, u24_max, 0, limit - 1);
    CHECK_FALSE(partial_receiver.feed(sealed_header).status);
    require_receive_closed(partial_receiver);

    kth::node::sv2::noise_detail::noise_cipher cipher;
    cipher.key = keys.initiator_to_responder;
    auto const sealed = cipher.encrypt({}, payload);
    REQUIRE(sealed);
    CHECK(cipher.nonce == 1);
    kth::node::sv2::noise_detail::noise_cipher opener;
    opener.key = cipher.key;
    auto bad = *sealed;
    bad.back() ^= 1;
    auto const tampered = opener.decrypt({}, bad);
    REQUIRE_FALSE(tampered);
    CHECK(tampered.error() == noise_error::authentication);
    CHECK(opener.nonce == 0);
    CHECK(opener.decrypt({}, *sealed));
    CHECK(opener.nonce == 1);
    opener.nonce = limit;
    CHECK_FALSE(opener.decrypt({}, *sealed));
    CHECK(opener.nonce == limit);
}

TEST_CASE("sv2 noise errors have a description for the log", "[sv2 noise]") {
    for (auto const error : {noise_error::invalid_secret_key, noise_error::invalid_public_key,
            noise_error::invalid_authority_key, noise_error::invalid_validity,
            noise_error::malformed_certificate, noise_error::certificate_version,
            noise_error::certificate_out_of_date, noise_error::certificate_signature,
            noise_error::flight_size, noise_error::handshake_used, noise_error::authentication,
            noise_error::malformed_frame, noise_error::frame_too_large, noise_error::nonce_exhausted,
            noise_error::closed, noise_error::clock_out_of_range}) {
        CHECK_FALSE(to_string(error).empty());
        CHECK(to_string(error) != to_string(noise_error(int(error) + 100)));
    }
}
