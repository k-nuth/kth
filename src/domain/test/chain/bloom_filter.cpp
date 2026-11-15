// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace kth;
using namespace kd;
using chain::bloom_filter;

namespace {

data_chunk bytes(std::string const& hex) {
    auto const result = decode_base16(hex);
    REQUIRE(result);
    return *result;
}

// The filter as `filterload` carries it: the bit field with its length, the
// number of hash functions, the tweak and the flags.
std::string wire(bloom_filter const& filter) {
    data_chunk raw;
    REQUIRE(filter.bits().size() < 0xfd);
    raw.push_back(uint8_t(filter.bits().size()));
    raw.insert(raw.end(), filter.bits().begin(), filter.bits().end());
    for (auto const value : {filter.hash_functions(), filter.tweak()}) {
        for (int shift = 0; shift < 32; shift += 8) raw.push_back(uint8_t(value >> shift));
    }
    raw.push_back(filter.flags());
    return encode_base16(raw);
}

// Two inputs and three outputs. The first input spends output 3 of a
// transaction whose hash is all 0x11 and carries a signature and the public
// key 02aa..aa; the second spends output 0 of the hash 00 01 02 .. 1f and
// carries deadbeef. The outputs pay to the key hash 22..22, to the public key
// 03bb..bb, and carry the data cafe1234.
chain::transaction watched() {
    auto const raw = bytes("02000000021111111111111111111111111111111111111111111111111111111111111111030000006a4730444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444444442102aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaffffffff000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f000000000504deadbeeffeffffff03e8030000000000001976a914222222222222222222222222222222222222222288acd007000000000000232103bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbac0000000000000000066a04cafe123400000000");
    byte_reader reader(raw);
    auto result = chain::transaction::from_data(reader, true);
    REQUIRE(result);
    return *result;
}

hash_digest previous_first() {
    hash_digest hash{};
    hash.fill(0x11);
    return hash;
}

hash_digest previous_second() {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) hash[i] = uint8_t(i);
    return hash;
}

auto const key_hash = "2222222222222222222222222222222222222222";
auto const public_key = "03bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
auto const input_key = "02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

bloom_filter with(std::string const& element, uint8_t flags) {
    auto filter = bloom_filter::sized(10, 0.000001, 5, flags);
    filter.insert(bytes(element));
    return filter;
}

} // namespace

TEST_CASE("bloom filter of three elements serializes as other nodes build it", "[bloom filter]") {
    for (auto const& [tweak, expected] : {
            std::pair<uint32_t, char const*>{0, "03614e9b050000000000000001"},
            std::pair<uint32_t, char const*>{2147483649u, "03ce4299050000000100008001"}}) {
        CAPTURE(tweak);
        auto filter = bloom_filter::sized(3, 0.01, tweak, bloom_filter::update_all);
        for (auto const element : {"99108ad8ed9bb6274d3980bab5a85c048f0950c8",
                "b5a2c786d9ef4658287ced5914b37a1b4aa32eee", "b9300670b4c5366e95b2699e8b18bc75e5f729c5"}) {
            filter.insert(bytes(element));
            CHECK(filter.contains(bytes(element)));
        }
        // One bit different from the first element.
        CHECK_FALSE(filter.contains(bytes("19108ad8ed9bb6274d3980bab5a85c048f0950c8")));
        CHECK(wire(filter) == expected);
        CHECK(filter.within_limits());
    }
}

TEST_CASE("bloom filter sizes stay within what a peer may load", "[bloom filter]") {
    // More elements than the largest filter holds at that rate: both limits.
    auto const largest = bloom_filter::sized(1000000, 0.0000001, 0, bloom_filter::update_none);
    CHECK(largest.bits().size() == bloom_filter::max_size);
    CHECK(largest.hash_functions() <= bloom_filter::max_hash_functions);
    CHECK(largest.within_limits());
    auto const many_functions = bloom_filter::sized(1, 0.0000000001, 0, bloom_filter::update_none);
    CHECK(many_functions.hash_functions() <= bloom_filter::max_hash_functions);

    CHECK(bloom_filter(data_chunk(bloom_filter::max_size, 0), 50, 0, 0).within_limits());
    CHECK_FALSE(bloom_filter(data_chunk(bloom_filter::max_size + 1, 0), 50, 0, 0).within_limits());
    CHECK_FALSE(bloom_filter(data_chunk(8, 0), 51, 0, 0).within_limits());

    // No bytes: everything is in it, and nothing can be added. No hash
    // functions: the same.
    bloom_filter empty;
    CHECK(empty.contains(bytes("00")));
    empty.insert(bytes("00"));
    CHECK(empty.bits().empty());
    bloom_filter const no_functions(data_chunk(8, 0), 0, 0, 0);
    CHECK(no_functions.contains(bytes("00")));
    // A filter with nothing in it holds nothing.
    bloom_filter const nothing(data_chunk(8, 0), 5, 0, 0);
    CHECK_FALSE(nothing.contains(bytes("00")));
}

TEST_CASE("bloom filter matches a transaction by what it carries", "[bloom filter]") {
    auto const tx = watched();
    auto const id = tx.hash();
    REQUIRE(encode_base16(id) == "67342ccee1fc3f163f5944ed78bc1aecd62045354c5e6c9262b2f3adf1a1c3aa");

    SECTION("its hash") {
        auto filter = bloom_filter::sized(10, 0.000001, 5, bloom_filter::update_all);
        filter.insert(id);
        CHECK(filter.is_relevant_and_update(tx));
    }
    SECTION("a key hash one output pays to, and then the outpoint of that output") {
        auto filter = with(key_hash, bloom_filter::update_all);
        REQUIRE(wire(filter)
            == "230819000020020400004080000000800800800040001008000400800000000000004000130000000500000001");
        CHECK_FALSE(filter.contains_outpoint(chain::point(id, 0)));
        CHECK(filter.match_and_insert_outputs(tx));
        CHECK(filter.contains_outpoint(chain::point(id, 0)));
        // The same bytes another implementation holds after the same match.
        CHECK(wire(filter)
            == "23481901083002140004c080004001800800900640011008000408808201000002044000130000000500000001");
        // Only the matched output was added.
        CHECK_FALSE(filter.contains_outpoint(chain::point(id, 1)));
        CHECK_FALSE(filter.contains_outpoint(chain::point(id, 2)));
    }
    SECTION("no update: the match alone") {
        auto filter = with(key_hash, bloom_filter::update_none);
        auto const before = filter;
        CHECK(filter.is_relevant_and_update(tx));
        CHECK(filter == before);
    }
    SECTION("public keys only: a key hash output is matched and not added") {
        auto filter = with(key_hash, bloom_filter::update_p2pubkey_only);
        auto const before = filter;
        CHECK(filter.match_and_insert_outputs(tx));
        CHECK(filter == before);
    }
    SECTION("public keys only: an output that pays to the key is added") {
        auto filter = with(public_key, bloom_filter::update_p2pubkey_only);
        CHECK(filter.match_and_insert_outputs(tx));
        CHECK(filter.contains_outpoint(chain::point(id, 1)));
        CHECK_FALSE(filter.contains_outpoint(chain::point(id, 0)));
    }
    SECTION("data an output carries") {
        auto filter = with("cafe1234", bloom_filter::update_all);
        CHECK(filter.match_and_insert_outputs(tx));
        CHECK(filter.contains_outpoint(chain::point(id, 2)));
        CHECK_FALSE(filter.match_inputs(tx));
    }
    SECTION("an outpoint an input spends") {
        auto filter = bloom_filter::sized(10, 0.000001, 5, bloom_filter::update_all);
        filter.insert_outpoint(chain::point(previous_first(), 3));
        CHECK(filter.match_inputs(tx));
        CHECK_FALSE(filter.match_and_insert_outputs(tx));
        CHECK(filter.is_relevant_and_update(tx));
        // Another output of the same transaction is not the one spent.
        auto other = bloom_filter::sized(10, 0.000001, 5, bloom_filter::update_all);
        other.insert_outpoint(chain::point(previous_first(), 2));
        CHECK_FALSE(other.is_relevant_and_update(tx));
        auto second = bloom_filter::sized(10, 0.000001, 5, bloom_filter::update_all);
        second.insert_outpoint(chain::point(previous_second(), 0));
        CHECK(second.match_inputs(tx));
    }
    SECTION("data an input script carries") {
        auto key = with(input_key, bloom_filter::update_all);
        CHECK(key.match_inputs(tx));
        CHECK_FALSE(key.match_and_insert_outputs(tx));
        auto data = with("deadbeef", bloom_filter::update_all);
        CHECK(data.match_inputs(tx));
    }
    SECTION("nothing of it") {
        auto filter = with("0123456789abcdef0123456789abcdef01234567", bloom_filter::update_all);
        auto const before = filter;
        CHECK_FALSE(filter.is_relevant_and_update(tx));
        CHECK(filter == before);
        // Part of a push is not the push.
        auto part = with("cafe12", bloom_filter::update_all);
        CHECK_FALSE(part.is_relevant_and_update(tx));
    }
}

namespace {

// A transaction of one input, with `input_script`, and one output, with
// `output_script`.
chain::transaction carrying(std::string const& input_script, std::string const& output_script) {
    chain::input::list inputs;
    inputs.emplace_back(chain::output_point{previous_first(), 0}, chain::script{bytes(input_script), false},
        0xffffffffu);
    chain::output::list outputs;
    outputs.emplace_back(uint64_t{1000}, chain::script{bytes(output_script), false}, chain::token_data_opt{});
    return chain::transaction{2, 0, std::move(inputs), std::move(outputs)};
}

} // namespace

TEST_CASE("bloom filter matches a push whatever opcode carries it", "[bloom filter]") {
    // cafe1234 as a direct push, and behind a one, two and four byte length.
    for (auto const script : {"04cafe1234", "4c04cafe1234", "4d0400cafe1234", "4e04000000cafe1234"}) {
        CAPTURE(script);
        auto outputs = with("cafe1234", bloom_filter::update_none);
        CHECK(outputs.match_and_insert_outputs(carrying("00", script)));
        CHECK(with("cafe1234", bloom_filter::update_none).match_inputs(carrying(script, "6a")));
    }
    // The pushes after the first are looked at too, and opcodes between them
    // carry nothing.
    auto later = with("cafe1234", bloom_filter::update_none);
    CHECK(later.match_and_insert_outputs(carrying("00", "6a02beef7604cafe1234")));
    // A push of no bytes is no element, not even for the filter of nothing.
    auto none = with("cafe1234", bloom_filter::update_none);
    CHECK_FALSE(none.match_and_insert_outputs(carrying("00", "004c00")));
}

TEST_CASE("bloom filter looks at nothing past the last whole push of a script", "[bloom filter]") {
    // The push before the one that is cut short still matches.
    auto before = with("cafe1234", bloom_filter::update_none);
    CHECK(before.match_and_insert_outputs(carrying("00", "04cafe123405beef")));
    CHECK(with("cafe1234", bloom_filter::update_none).match_inputs(carrying("04cafe12344c", "6a")));

    // What a cut push holds does not: five bytes announced, four there; and a
    // length prefix that is itself cut.
    for (auto const script : {"05cafe1234", "4c05cafe1234", "4d0500cafe1234", "4d04", "4e040000"}) {
        CAPTURE(script);
        auto cut = with("cafe1234", bloom_filter::update_none);
        CHECK_FALSE(cut.match_and_insert_outputs(carrying("00", script)));
        CHECK_FALSE(with("cafe1234", bloom_filter::update_none).match_inputs(carrying(script, "6a")));
    }
}
