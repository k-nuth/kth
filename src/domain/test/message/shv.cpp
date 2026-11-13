// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

using namespace kth;
using namespace kd;

namespace {

constexpr auto wire_version = message::version::level::maximum;

data_chunk bytes(char const* hex) {
    auto const result = decode_base16(hex);
    REQUIRE(result);
    return *result;
}

data_chunk request_bytes() {
    // A proof to the peak of block 5 in the commitment at height 10, by
    // height: the type, then both heights as CompactSize.
    return bytes("01000a05");
}

data_chunk response_bytes() {
    // Genesis proof to the root: status found, then the type, both heights,
    // the header, the proof and the target.
    return bytes("0100010000"
        "01000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "29ab5f49ffff001d1dac2b7c"
        "00"
        "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000");
}

data_chunk noncanonical(data_chunk raw, size_t offset) {
    auto const value = raw[offset];
    raw.erase(raw.begin() + offset);
    raw.insert(raw.begin() + offset, {0xfd, value, 0x00});
    return raw;
}

template <typename Message>
void check_boundaries(Message const& message) {
    auto const raw = to_data_chunk(message, wire_version);
    // No incomplete payload may succeed, regardless of where a read splits it.
    for (size_t size = 0; size < raw.size(); ++size) {
        INFO("payload prefix " << size);
        byte_reader reader(byte_span{raw.data(), size});
        REQUIRE( ! Message::from_data(reader, wire_version));
        data_chunk short_buffer(size);
        byte_writer writer(short_buffer);
        auto const written = message.to_data(writer, wire_version);
        REQUIRE( ! written);
        REQUIRE(written.error() == error::write_past_end_of_buffer);
    }
    auto trailing = raw;
    trailing.push_back(0);
    REQUIRE( ! from_data_chunk<Message>(trailing, wire_version));
}

} // namespace

TEST_CASE("SHV messages keep their wire fields and hash order", "[shv][message]") {
    auto const request_raw = request_bytes();
    auto const request = from_data_chunk<message::get_shv>(request_raw, wire_version);
    REQUIRE(request);
    REQUIRE(request->requests.size() == 1);
    REQUIRE(request->requests[0].type == 0);
    REQUIRE(request->requests[0].commitment_height == 10);
    REQUIRE(request->requests[0].block_height() == 5);
    REQUIRE(to_data_chunk(*request, wire_version) == request_raw);

    auto const response_raw = response_bytes();
    auto const response = from_data_chunk<message::shv>(response_raw, wire_version);
    REQUIRE(response);
    REQUIRE(response->responses.size() == 1);
    auto const& entry = response->responses[0];
    REQUIRE(entry.status == message::shv_status_found);
    REQUIRE(entry.type == message::shv_proof_to_root);
    REQUIRE(entry.commitment_height == 0);
    REQUIRE(entry.block_height == 0);
    REQUIRE(entry.proof.empty());
    REQUIRE(entry.target == bitcoin_hash(to_data_chunk(entry.header, true)));
    REQUIRE(encode_hash(entry.target) == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");
    REQUIRE(to_data_chunk(*response, wire_version) == response_raw);
    REQUIRE(message::heading(0, "getshv", 0, 0).type() == message::message_type::get_shv);
    REQUIRE(message::heading(0, "shv", 0, 0).type() == message::message_type::shv);
    REQUIRE(message::version::service::node_shv == (1U << 9));

    message::get_shv large;
    large.requests.push_back({3, 0x100000000ULL, {}});
    auto const large_raw = bytes("0103ff0000000001000000"
        "0000000000000000000000000000000000000000000000000000000000000000");
    REQUIRE(to_data_chunk(large, wire_version) == large_raw);
    REQUIRE(from_data_chunk<message::get_shv>(large_raw, wire_version) == large);
    for (auto const height : {uint64_t{0xfd}, uint64_t{0x10000}}) {
        large.requests[0].commitment_height = height;
        auto const raw = to_data_chunk(large, wire_version);
        REQUIRE(from_data_chunk<message::get_shv>(raw, wire_version) == large);
    }

    message::get_shv const empty_request;
    message::shv const empty_response;
    REQUIRE(to_data_chunk(empty_request, wire_version) == data_chunk{0});
    REQUIRE(to_data_chunk(empty_response, wire_version) == data_chunk{0});
    REQUIRE(from_data_chunk<message::get_shv>(data_chunk{0}, wire_version) == empty_request);
    REQUIRE(from_data_chunk<message::shv>(data_chunk{0}, wire_version) == empty_response);
}

TEST_CASE("SHV requests keep reserved types so each gets its own status", "[shv][message]") {
    message::shv_request request;
    request.block_height_or_hash[0] = 0xef;
    request.block_height_or_hash[7] = 0x12;
    REQUIRE(request.block_height() == 0x12000000000000efULL);
    for (uint8_t type = 0; type < 4; ++type) {
        request.type = type;
        REQUIRE(request.is_valid());
        REQUIRE(request.is_proof_to_root() == ((type & 1) != 0));
        REQUIRE(request.is_by_hash() == ((type & 2) != 0));
    }
    // A height is a CompactSize, so it always fits; only reserved bits make a
    // request invalid, and the request still parses so it can be answered.
    request.type = 0x04;
    REQUIRE( ! request.is_valid());
    message::get_shv const by_height{{request}};
    REQUIRE(to_data_chunk(by_height, wire_version) == bytes("010400ff" "ef000000000000" "12"));
    REQUIRE(from_data_chunk<message::get_shv>(to_data_chunk(by_height, wire_version), wire_version) == by_height);
    request.type = message::shv_by_hash;
    REQUIRE(request.is_valid());
    request.block_height_or_hash[31] = 0x5a;
    request.type |= 0x80;
    REQUIRE( ! request.is_valid());
    message::get_shv const by_hash{{request}};
    REQUIRE(to_data_chunk(by_hash, wire_version).size() == 1 + 1 + 1 + hash_size);
    REQUIRE(from_data_chunk<message::get_shv>(to_data_chunk(by_hash, wire_version), wire_version) == by_hash);
}

TEST_CASE("SHV requests by height carry the height as a CompactSize", "[shv][message]") {
    auto const at = [](uint64_t height) {
        message::shv_request request{0, 7, null_hash};
        for (size_t i = 0; i < sizeof(height); ++i) {
            request.block_height_or_hash[i] = uint8_t(height >> (i * 8));
        }
        return message::get_shv{{request}};
    };
    for (auto const& [height, hex] : std::vector<std::pair<uint64_t, char const*>>{
            {0, "01000700"}, {0xfc, "010007fc"}, {0xfd, "010007fdfd00"},
            {0x10000, "010007fe00000100"}, {0x100000000ULL, "010007ff0000000001000000"}}) {
        INFO("height " << height);
        auto const message = at(height);
        auto const raw = to_data_chunk(message, wire_version);
        REQUIRE(raw == bytes(hex));
        REQUIRE(raw.size() == message.serialized_size(wire_version));
        auto const parsed = from_data_chunk<message::get_shv>(raw, wire_version);
        REQUIRE(parsed);
        REQUIRE(parsed->requests[0].block_height() == height);
        REQUIRE(*parsed == message);
    }
    // A request by hash carries 32 bytes, never a CompactSize height.
    REQUIRE( ! from_data_chunk<message::get_shv>(bytes("010207" "05"), wire_version));
}

TEST_CASE("SHV entries carry a status and only a found one carries a proof", "[shv][message]") {
    auto const found = from_data_chunk<message::shv>(response_bytes(), wire_version);
    REQUIRE(found);
    message::shv answer{{found->responses[0]}};
    for (auto const status : {message::shv_status_not_found, message::shv_status_above_commitment,
            message::shv_status_commitment_above_tip, message::shv_status_reserved_type}) {
        message::shv_response refused;
        refused.status = status;
        REQUIRE(refused.serialized_size() == 1);
        answer.responses.push_back(refused);
    }
    answer.responses.push_back(found->responses[0]);
    auto const raw = to_data_chunk(answer, wire_version);
    REQUIRE(raw.size() == answer.serialized_size(wire_version));
    // Six entries: the genesis proof twice, and between them the four
    // refusals, each its status byte alone.
    auto const genesis = response_bytes();
    auto expected = data_chunk{6};
    expected.insert(expected.end(), genesis.begin() + 1, genesis.end());
    expected.insert(expected.end(), {1, 2, 3, 4});
    expected.insert(expected.end(), genesis.begin() + 1, genesis.end());
    REQUIRE(raw == expected);
    auto const parsed = from_data_chunk<message::shv>(raw, wire_version);
    REQUIRE(parsed);
    REQUIRE(*parsed == answer);
    check_boundaries(answer);

    // A status above reserved type is not a valid entry, read or written.
    for (uint8_t const unknown : {uint8_t{5}, uint8_t{0xff}}) {
        INFO("status " << int(unknown));
        auto const read = from_data_chunk<message::shv>(data_chunk{1, unknown}, wire_version);
        REQUIRE( ! read);
        REQUIRE(read.error() == error::illegal_value);
        message::shv bad;
        bad.responses.push_back({.status = unknown});
        data_chunk buffer(16);
        byte_writer writer(buffer);
        auto const written = bad.to_data(writer, wire_version);
        REQUIRE( ! written);
        REQUIRE(written.error() == error::illegal_value);
    }
}

TEST_CASE("SHV wire parsing rejects truncation noncanonical sizes and oversized batches", "[shv][message]") {
    auto const request = from_data_chunk<message::get_shv>(request_bytes(), wire_version);
    REQUIRE(request);
    check_boundaries(*request);

    auto response = from_data_chunk<message::shv>(response_bytes(), wire_version);
    REQUIRE(response);
    response->responses[0].proof = {null_hash, response->responses[0].target};
    check_boundaries(*response);

    for (auto const offset : {size_t{0}, size_t{2}, size_t{3}}) {
        REQUIRE( ! from_data_chunk<message::get_shv>(noncanonical(request_bytes(), offset), wire_version));
    }
    for (auto const offset : {size_t{0}, size_t{3}, size_t{4}, size_t{85}}) {
        REQUIRE( ! from_data_chunk<message::shv>(noncanonical(response_bytes(), offset), wire_version));
    }
    auto const oversized = data_chunk{101};
    auto const too_many_requests = from_data_chunk<message::get_shv>(oversized, wire_version);
    REQUIRE( ! too_many_requests);
    REQUIRE(too_many_requests.error() == error::invalid_size);
    auto const too_many_responses = from_data_chunk<message::shv>(oversized, wire_version);
    REQUIRE( ! too_many_responses);
    REQUIRE(too_many_responses.error() == error::invalid_size);
    auto proof = response_bytes();
    proof[85] = 65;
    auto const too_many_siblings = from_data_chunk<message::shv>(proof, wire_version);
    REQUIRE( ! too_many_siblings);
    REQUIRE(too_many_siblings.error() == error::invalid_size);

    message::get_shv maximum;
    maximum.requests.resize(message::max_shv_requests);
    auto const maximum_raw = to_data_chunk(maximum, wire_version);
    REQUIRE(from_data_chunk<message::get_shv>(maximum_raw, wire_version) == maximum);
    response->responses[0].proof.resize(64);
    response->responses.resize(message::max_shv_requests, response->responses.front());
    auto const maximum_response = to_data_chunk(*response, wire_version);
    REQUIRE(from_data_chunk<message::shv>(maximum_response, wire_version) == *response);
}
