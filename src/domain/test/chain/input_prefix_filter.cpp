// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/domain/message/compact_filter.hpp>

using namespace kth;
using namespace kd;

namespace {

using chain::input_prefix_filter_error;

// Blocks made by a reference written from the filter's specification alone,
// in Python, with no node's code (scripts/rpa_filter_vectors.py in
// BitcoinCash1/knuth-bch-startos): the value of each input after the
// coinbase, the filter's values, its served bytes, its filter hash, and its
// filter header with the blocks chained in this order from all zeros. Hashes
// are in stored byte order.
//
// The blocks: a coinbase alone; one input; two inputs with equal values (the
// second one's sequence ground to it); two transactions of 2 and 3 inputs;
// one transaction of 31 inputs, whose 31st value is not among the first 30.
struct block_vector {
    std::string_view name;
    // Empty for the block of 31 inputs, which thirty_one_inputs() builds.
    std::string_view block_hex;
    // For each transaction after the coinbase, the value of each input.
    std::vector<std::vector<uint16_t>> input_values;
    std::vector<uint16_t> values;
    std::string_view filter_hex;
    std::string_view hash_hex;
    std::string_view header_hex;
};

std::vector<block_vector> const& block_vectors() {
    static std::vector<block_vector> const vectors{
    {
        "a coinbase alone",
        "0000002000000000000000000000000000000000000000000000000000000000000000009d6475cc74d5e2c36a9c5fa5"
        "64b25676a0442a86eff51081bd077a9fc0919d2b00f15365ffff7f200000000001010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff06010003727061ffffffff0100f2052a010000000151"
        "00000000",
        {},
        {},
        "00",
        "1406e05881e299367766d313e26c05564ec91bf721d31726bd6e46e60689539a",
        "a80ba8d54948ed417954508791bcf9321bcda26c160b77ab695258281c0d3e75",
    },
    {
        "one input",
        "00000020679d3aae7bad61757b5e1e3e225764d3a90a72d6f383e104953f82f36c7d5235382edb97970c0e4c882a3736"
        "d227780325ae84c0fdf065b1402c61a949e6ced658f35365ffff7f200000000002010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff06010103727061ffffffff0100f2052a010000000151"
        "0000000002000000011111111111111111111111111111111111111111111111111111111111111111000000000151ff"
        "ffffff01e803000000000000015100000000",
        {{0x9408}},
        {0x9408},
        "010f8a0400",
        "e1452af78ceb4f079714cb094c3f5c69bbb1a0d38bb90fd4fc63365649474fce",
        "1c0123a83da1787bd17ee2b07312e3a2769ab6d6fd2963b97fcd28e595b75c6f",
    },
    {
        "two inputs sharing a prefix",
        "000000208c3745485793913b4370e90cfc2edba60842edfc107f116fa447338bf101f23ab67e94cdd0d6a920578271a0"
        "c13997f863e77ae4346962950e36c392dfdc1f56b0f55365ffff7f200000000002010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff06010203727061ffffffff0100f2052a010000000151"
        "0000000002000000022222222222222222222222222222222222222222222222222222222222222222000000000151ff"
        "ffffff222222222222222222222222222222222222222222222222222222222222222201000000015186ed000001e803"
        "000000000000015100000000",
        {{0xc3ed, 0xc3ed}},
        {0xc3ed},
        "010fa1f680",
        "f423f13e1b97819945f324ed136d136f15a71656b4ae0fd031ff6e287257879c",
        "8f18899a3f80fdbf3ca6f15203b5c016fb0577cf5853ee912d4c88e6685192ca",
    },
    {
        "two transactions",
        "00000020747869088322e048c5f4dd87ec0a1b1eef99b91b78d8c06b2291e41bad869a2c48e727a0229de260837dba40"
        "8a3a308097fce37f6b7950954f250f60aa61d74708f85365ffff7f200000000003010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff06010303727061ffffffff0100f2052a010000000151"
        "00000000020000000233333333333333333333333333333333333333333333333333333333333333330000000000ffff"
        "ffff33333333333333333333333333333333333333333333333333333333333333330100000000ffffffff01e8030000"
        "000000000151000000000200000003444444444444444444444444444444444444444444444444444444444444444400"
        "00000015140000000000000000000000000000000000000000feffffff44444444444444444444444444444444444444"
        "444444444444444444444444440100000015140101010101010101010101010101010101010101fdffffff4444444444"
        "444444444444444444444444444444444444444444444444444444020000001514020202020202020202020202020202"
        "0202020202fcffffff01e803000000000000015100000000",
        {{0xc535, 0xa8e6}, {0x5e33, 0x0c06, 0x231f}},
        {0x0c06, 0x231f, 0x5e33, 0xa8e6, 0xc535},
        "050d3019719b6299566e2780",
        "eea30a94c8acfa84935aa0a4a2a442a3e709ae60b21ed77e108831a5a9d1565d",
        "6e50997c60f71d3ceb2905e2ed7cec28d294c0c51b645e5508356b42f67b3a3b",
    },
    {
        "31 inputs",
        "",
        {{0xd2fd, 0x51de, 0xebfa, 0x9376, 0x54bf, 0x96aa, 0xaf16, 0x4d39, 0xb033, 0x8ffe, 0x9885, 0x81ef,
          0x1d3c, 0xb75a, 0x171b, 0xb08d, 0x1013, 0x1874, 0xe39d, 0xfc15, 0xed4c, 0x3542, 0x7e8e, 0x02ed,
          0x6ce7, 0xc3ac, 0xde98, 0x5c3e, 0x9798, 0xd207, 0xa938}},
        {0x02ed, 0x1013, 0x171b, 0x1874, 0x1d3c, 0x3542, 0x4d39, 0x51de, 0x54bf, 0x5c3e, 0x6ce7, 0x7e8e,
         0x81ef, 0x8ffe, 0x9376, 0x96aa, 0x9798, 0x9885, 0xaf16, 0xb033, 0xb08d, 0xb75a, 0xc3ac, 0xd207,
         0xd2fd, 0xde98, 0xe39d, 0xebfa, 0xed4c, 0xfc15},
        "1e0a5dbc935841598c8fc037dfbc52ae1b7ff153e69db0f41ede19a0ee1dbf5224742d566f0a5d2d8f6dcdc82e174a975920",
        "5773e5c29f136644b9f88d2d3df1cd527d9ed5a68a6773d25b113dc50930f8c0",
        "4b595d03261b964c360e1d9f39a670ada0dd94c5a6298ef947334b7026792489",
    },
    };
    return vectors;
}

data_chunk from_hex(std::string_view hex) {
    auto decoded = decode_base16(hex);
    REQUIRE(decoded);
    return std::move(*decoded);
}

chain::transaction coinbase() {
    return chain::transaction{1, 0,
        chain::input::list{chain::input{chain::output_point{null_hash, chain::point::null_index},
            chain::script{data_chunk{0x51}, false}, max_input_sequence}},
        chain::output::list{chain::output{5000, chain::script{data_chunk{0x51}, false}, chain::token_data_opt{}}}};
}

// A transaction of `count` inputs that spend outputs 0 up of `txid`, with
// empty scripts and final sequences, paying 1,000 satoshis to OP_1.
chain::transaction spend_of(hash_digest const& txid, uint32_t count) {
    chain::input::list inputs;
    for (uint32_t i = 0; i < count; ++i) {
        inputs.emplace_back(chain::output_point{txid, i}, chain::script{}, max_input_sequence);
    }
    return chain::transaction{2, 0, std::move(inputs),
        chain::output::list{chain::output{1000, chain::script{data_chunk{0x51}, false}, chain::token_data_opt{}}}};
}

chain::block block_of(chain::transaction::list transactions) {
    chain::header const header{0x20000000, null_hash, null_hash, 1700000000, 0x207fffff, 0};
    return chain::block{header, std::move(transactions)};
}

// The fifth block's transaction after the coinbase: 31 inputs spending
// outputs 0 to 30 of the txid 0x55 repeated.
chain::transaction thirty_one_inputs() {
    hash_digest txid{};
    txid.fill(0x55);
    return spend_of(txid, 31);
}

chain::block block_at(block_vector const& vector) {
    if (vector.block_hex.empty()) {
        return block_of({coinbase(), thirty_one_inputs()});
    }
    auto const raw = from_hex(vector.block_hex);
    byte_reader reader(raw);
    auto block = chain::block::from_data(reader);
    REQUIRE(block);
    return std::move(*block);
}

chain::input_prefix_filter filter_of(std::vector<uint16_t> values) {
    return chain::input_prefix_filter{std::move(values)};
}

// The values from 0 up to `count`, one each.
std::vector<uint16_t> first_values(uint32_t count) {
    std::vector<uint16_t> values;
    for (uint32_t value = 0; value < count; ++value) {
        values.push_back(uint16_t(value));
    }
    return values;
}

std::string head_of(data_chunk const& bytes, size_t size) {
    return encode_base16(data_chunk(bytes.begin(), bytes.begin() + std::ptrdiff_t(size)));
}

std::string tail_of(data_chunk const& bytes, size_t size) {
    return encode_base16(data_chunk(bytes.end() - std::ptrdiff_t(size), bytes.end()));
}

} // namespace

// Start Test Suite: input prefix filter tests

TEST_CASE("input prefix filters of the vector blocks are the reference's, with their hashes and headers",
          "[input prefix filter]") {
    REQUIRE(block_vectors().size() == 5);
    hash_digest header = null_hash;
    for (auto const& vector : block_vectors()) {
        CAPTURE(vector.name);
        auto const block = block_at(vector);
        auto const& transactions = block.transactions();
        REQUIRE(transactions.size() == vector.input_values.size() + 1);
        // The value of every input after the coinbase, the 31st one included.
        for (size_t t = 1; t < transactions.size(); ++t) {
            auto const& inputs = transactions[t].inputs();
            REQUIRE(inputs.size() == vector.input_values[t - 1].size());
            for (size_t i = 0; i < inputs.size(); ++i) {
                CAPTURE(t, i);
                CHECK(chain::input_prefix(inputs[i]) == vector.input_values[t - 1][i]);
            }
        }

        auto const filter = chain::build_input_prefix_filter(block);
        CHECK(filter.values == vector.values);
        auto const served = chain::to_data(filter);
        CHECK(encode_base16(served) == std::string{vector.filter_hex});
        CHECK(encode_base16(chain::input_prefix_filter_hash(filter)) == std::string{vector.hash_hex});
        auto const next = chain::input_prefix_filter_header(filter, header);
        CHECK(encode_base16(next) == std::string{vector.header_hex});
        // The definition, spelled out.
        CHECK(next == bitcoin_hash(splice(bitcoin_hash(served), header)));
        header = next;

        auto const parsed = chain::parse_input_prefix_filter(served);
        REQUIRE(parsed);
        CHECK(*parsed == filter);
    }
}

TEST_CASE("only the first 30 inputs of a transaction after the coinbase are values", "[input prefix filter]") {
    auto const transaction = thirty_one_inputs();
    auto const& inputs = transaction.inputs();
    REQUIRE(inputs.size() == 31);
    auto const thirty_first = chain::input_prefix(inputs[30]);
    auto const thirtieth = chain::input_prefix(inputs[29]);
    REQUIRE(thirty_first == 0xa938);
    REQUIRE(thirtieth == 0xd207);

    auto const filter = chain::build_input_prefix_filter(block_of({coinbase(), transaction}));
    CHECK(filter.values.size() == 30);
    CHECK( ! std::ranges::binary_search(filter.values, thirty_first));
    CHECK(std::ranges::binary_search(filter.values, thirtieth));
    CHECK(chain::has_input_prefix(filter, thirty_first, 16) == false);

    // The same inputs with the 31st moved first: now it is a value, and the
    // one pushed to the 31st place is not.
    chain::input::list moved{inputs[30]};
    moved.insert(moved.end(), inputs.begin(), inputs.begin() + 30);
    chain::transaction const reordered{transaction.version(), transaction.locktime(), std::move(moved),
        chain::output::list(transaction.outputs())};
    auto const other = chain::build_input_prefix_filter(block_of({coinbase(), reordered}));
    CHECK(other.values.size() == 30);
    CHECK(std::ranges::binary_search(other.values, thirty_first));
    CHECK( ! std::ranges::binary_search(other.values, thirtieth));

    // A second transaction has its own first 30.
    hash_digest txid{};
    txid.fill(0x66);
    auto const both = chain::build_input_prefix_filter(block_of({coinbase(), transaction, spend_of(txid, 1)}));
    CHECK(std::ranges::binary_search(both.values, chain::input_prefix(spend_of(txid, 1).inputs()[0])));
    CHECK( ! std::ranges::binary_search(both.values, thirty_first));

    // The coinbase is no transaction of the filter's, whatever its input.
    auto const alone = chain::build_input_prefix_filter(block_of({transaction}));
    CHECK(alone.values.empty());
    CHECK(encode_base16(chain::to_data(alone)) == "00");
}

TEST_CASE("a block of 30,000 inputs has enough values for the bitmap", "[input prefix filter]") {
    // Transaction j (1 to 1,000) spends outputs 0 to 29 of the txid that is j
    // in four little-endian bytes and 28 zero bytes.
    chain::transaction::list transactions{coinbase()};
    for (uint32_t j = 1; j <= 1000; ++j) {
        hash_digest txid{};
        for (size_t b = 0; b < 4; ++b) {
            txid[b] = uint8_t(j >> (8 * b));
        }
        transactions.push_back(spend_of(txid, 30));
    }
    auto const filter = chain::build_input_prefix_filter(block_of(std::move(transactions)));
    REQUIRE(filter.values.size() == 24051);
    CHECK(chain::input_prefix_rice_parameter(filter.values.size()) == chain::input_prefix_bitmap);
    CHECK(std::vector<uint16_t>(filter.values.begin(), filter.values.begin() + 4)
        == std::vector<uint16_t>{0x0001, 0x0002, 0x000e, 0x000f});
    CHECK(std::vector<uint16_t>(filter.values.end() - 4, filter.values.end())
        == std::vector<uint16_t>{0xfff9, 0xfffa, 0xfffc, 0xfffd});

    // The count, the byte 16 and the 8,192 bytes of the bitmap.
    auto const served = chain::to_data(filter);
    REQUIRE(served.size() == 8196);
    CHECK(head_of(served, 16) == "fdf35d106003cf5803bc3d88c206f1b0");
    CHECK(tail_of(served, 8) == "012cdca4351184ec");
    CHECK(encode_base16(chain::input_prefix_filter_hash(filter))
        == "a067316c9de0820f3a0d59e8f8f3afd1faa9fdabe19fcbd0240fc586bd0c0a5c");
    auto const parsed = chain::parse_input_prefix_filter(served);
    REQUIRE(parsed);
    CHECK(*parsed == filter);
}

TEST_CASE("the byte after the count comes from the count: k from 15 down to 1, then the bitmap", "[input prefix filter]") {
    // The first count of each k, and the bitmap from 19,197 values.
    std::vector<std::pair<size_t, uint8_t>> const starts{
        {1, 15}, {2, 14}, {3, 13}, {7, 12}, {15, 11}, {30, 10}, {61, 9}, {123, 8}, {246, 7}, {494, 6},
        {992, 5}, {1997, 4}, {4051, 3}, {8356, 2}, {18055, 1}, {19197, 16}};
    for (auto const& [count, k] : starts) {
        CAPTURE(count);
        CHECK(chain::input_prefix_rice_parameter(count) == k);
        if (count > 1) {
            // The count before a band is the last of the band before.
            auto const before = chain::input_prefix_rice_parameter(count - 1);
            CHECK(before == (k == chain::input_prefix_bitmap ? 1 : k + 1));
        }
    }
    CHECK(chain::input_prefix_rice_parameter(19196) == 1);
    CHECK(chain::input_prefix_rice_parameter(65536) == chain::input_prefix_bitmap);
    // k = 0 is never used: every count has a k from 15 down to 1, or the
    // bitmap, and they only go down until the bitmap.
    uint8_t previous = 15;
    for (size_t count = 1; count < chain::input_prefix_bitmap_from; ++count) {
        auto const k = chain::input_prefix_rice_parameter(count);
        REQUIRE(k >= 1);
        REQUIRE(k <= previous);
        previous = k;
    }
    // No values carry no byte, and there are no more than 65,536 values.
    CHECK(chain::input_prefix_rice_parameter(0) == 0);
    CHECK(chain::input_prefix_rice_parameter(65537) == 0);
}

TEST_CASE("sets of known encodings, from one value to every value", "[input prefix filter]") {
    // One value: k = 15, a zero-bit and the value's low 15 bits.
    CHECK(encode_base16(chain::to_data(filter_of({0}))) == "010f0000");
    // 65,535: one one-bit, then a zero-bit and 15 one-bits.
    CHECK(encode_base16(chain::to_data(filter_of({65535}))) == "010fbfff80");
    // 1 and 2: k = 14, two gaps of 1, padded with two zero-bits.
    CHECK(encode_base16(chain::to_data(filter_of({1, 2}))) == "020e00020004");
    // The set, whatever order and repeats the caller gives.
    CHECK(chain::to_data(filter_of({2, 1, 2})) == chain::to_data(filter_of({1, 2})));

    // The largest Rice count, 19,196 values, at k = 1 (a zero-bit and the
    // low bit for each gap: 00 for the first, 01 after), and the first
    // bitmap count, 19,197 values, the reference's bytes.
    auto const rice = chain::to_data(filter_of(first_values(19196)));
    REQUIRE(rice.size() == 4803);
    CHECK(head_of(rice, 8) == "fdfc4a0115555555");
    CHECK(encode_base16(bitcoin_hash(rice)) == "c87c720767d86452f2dfde01c6a5300a755a1fc9eabda48f4d4b56dc4ad17fea");
    REQUIRE(chain::parse_input_prefix_filter(rice));
    CHECK(chain::parse_input_prefix_filter(rice)->values == first_values(19196));
    auto const bitmap = chain::to_data(filter_of(first_values(19197)));
    REQUIRE(bitmap.size() == 8196);
    CHECK(head_of(bitmap, 8) == "fdfd4a10ffffffff");
    CHECK(tail_of(bitmap, 8) == "0000000000000000");
    CHECK(encode_base16(bitcoin_hash(bitmap)) == "355b13f4d81ff87e20fc33ce4ef7931acdbb3867f7b128bb71286322d3850001");
    REQUIRE(chain::parse_input_prefix_filter(bitmap));
    CHECK(chain::parse_input_prefix_filter(bitmap)->values == first_values(19197));

    // Every even value: N = 32,768, the byte 16 and a bitmap of aa.
    std::vector<uint16_t> even;
    for (uint32_t value = 0; value < 65536; value += 2) {
        even.push_back(uint16_t(value));
    }
    data_chunk even_bytes{0xfd, 0x00, 0x80, 0x10};
    even_bytes.insert(even_bytes.end(), 8192, 0xaa);
    auto const even_filter = filter_of(even);
    CHECK(chain::to_data(even_filter) == even_bytes);
    CHECK(encode_base16(chain::input_prefix_filter_hash(even_filter))
        == "b0abac56021c4436c49a9de58243b2d5594633503c83cddd2e190da6a5be37b7");
    auto const even_parsed = chain::parse_input_prefix_filter(even_bytes);
    REQUIRE(even_parsed);
    CHECK(even_parsed->values == even);

    // Every value: N = 65,536 in a four-byte count, the byte 16 and a bitmap
    // of ff.
    auto const every = first_values(65536);
    data_chunk every_bytes{0xfe, 0x00, 0x00, 0x01, 0x00, 0x10};
    every_bytes.insert(every_bytes.end(), 8192, 0xff);
    auto const every_filter = filter_of(every);
    CHECK(chain::to_data(every_filter) == every_bytes);
    CHECK(encode_base16(chain::input_prefix_filter_hash(every_filter))
        == "79d65549e1801072cda4736a9b36ee687cdb2ff6b94139437f242ddfc1251c01");
    auto const every_parsed = chain::parse_input_prefix_filter(every_bytes);
    REQUIRE(every_parsed);
    CHECK(every_parsed->values == every);
    // Every prefix of every length is in it.
    CHECK(chain::has_input_prefix(every_filter, 0, 0) == true);
    CHECK(chain::has_input_prefix(every_filter, 0x7, 4) == true);
    CHECK(chain::has_input_prefix(every_filter, 0x1234, 16) == true);
}

TEST_CASE("a filter that is not the one encoding of a set is refused", "[input prefix filter]") {
    struct refused {
        std::string_view hex;
        input_prefix_filter_error error;
        std::string_view what;
    };
    std::vector<refused> const cases{
        {"", input_prefix_filter_error::truncated, "no count"},
        {"01", input_prefix_filter_error::truncated, "a count and no Rice parameter"},
        {"010f", input_prefix_filter_error::truncated, "no bits for the value"},
        {"0111000000", input_prefix_filter_error::rice_parameter, "a byte above 16"},
        {"0110000000", input_prefix_filter_error::rice_parameter, "the bitmap's byte for one value"},
        {"020e000a0000", input_prefix_filter_error::repeated_value, "5 and then a gap of 0"},
        {"010fc00000", input_prefix_filter_error::value_too_large, "a first gap of 65,536"},
        {"020eefffc00080", input_prefix_filter_error::value_too_large, "65,535 and then a gap of 1"},
        {"020e000a", input_prefix_filter_error::truncated, "two values counted, one coded"},
        {"010f00070009", input_prefix_filter_error::trailing_bytes, "one value counted, two coded"},
        {"020e00000005", input_prefix_filter_error::padding, "0 and 1, the last padding bit set"},
        {"0000", input_prefix_filter_error::trailing_bytes, "no values, then a byte"},
        {"010f000700", input_prefix_filter_error::trailing_bytes, "one value, then a zero byte"},
        {"fd01000f0007", input_prefix_filter_error::count, "a count of 1 in three bytes"},
        {"fe010001000000", input_prefix_filter_error::count, "a count of 65,537"},
    };
    for (auto const& item : cases) {
        CAPTURE(item.what);
        auto const parsed = chain::parse_input_prefix_filter(from_hex(item.hex));
        REQUIRE( ! parsed);
        CHECK(parsed.error() == item.error);
    }

    // A run of one-bits is refused once it passes the largest value, before
    // its end: 32,776 of them with k = 1, the parameter of 19,196 values,
    // past 65,535 at the 32,768th.
    data_chunk run{0xfd, 0xfc, 0x4a, 0x01};
    run.insert(run.end(), 4097, 0xff);
    auto const long_run = chain::parse_input_prefix_filter(run);
    REQUIRE( ! long_run);
    CHECK(long_run.error() == input_prefix_filter_error::value_too_large);

    // A good filter cut anywhere, or with a byte after it, is refused.
    auto const good = from_hex(block_vectors()[3].filter_hex);
    REQUIRE(chain::parse_input_prefix_filter(good));
    for (size_t size = 0; size < good.size(); ++size) {
        CAPTURE(size);
        auto const cut = chain::parse_input_prefix_filter(byte_span{good.data(), size});
        REQUIRE( ! cut);
        CHECK(cut.error() == input_prefix_filter_error::truncated);
    }
    for (uint8_t extra : {0x00, 0x80}) {
        auto longer = good;
        longer.push_back(extra);
        auto const parsed = chain::parse_input_prefix_filter(longer);
        REQUIRE( ! parsed);
        CHECK(parsed.error() == input_prefix_filter_error::trailing_bytes);
    }
}

TEST_CASE("a filter whose Rice parameter is not its count's is refused", "[input prefix filter]") {
    // One value, 3: the one encoding is 010f0003 (k = 15). The same value
    // written with k = 0 or k = 14 is a second encoding of the same set, so
    // it is refused, and each set has one filter and one hash.
    auto const canonical = chain::parse_input_prefix_filter(from_hex("010f0003"));
    REQUIRE(canonical);
    CHECK(canonical->values == std::vector<uint16_t>{3});
    for (auto const* hex : {"0100e0", "010e0006"}) {
        INFO(hex);
        auto const parsed = chain::parse_input_prefix_filter(from_hex(hex));
        REQUIRE( ! parsed);
        CHECK(parsed.error() == input_prefix_filter_error::rice_parameter);
    }
}

TEST_CASE("a bitmap is refused for another count, with another number of values, or cut or longer", "[input prefix filter]") {
    // Each built as the reference builds it, and checked by length and hash.
    struct refused {
        data_chunk bytes;
        size_t size;
        std::string_view hash;
        input_prefix_filter_error error;
        std::string_view what;
    };

    // Rice coding at k = 1 for 19,197 values (0 to 19,196): 00, then 01 for
    // each gap of 1, padded with zero-bits.
    data_chunk rice_19197{0xfd, 0xfd, 0x4a, 0x01, 0x15};
    rice_19197.insert(rice_19197.end(), 4798, 0x55);
    rice_19197.push_back(0x40);

    // The bitmap of 19,197 values (0 to 19,195 and 65,535) under a count of 19,196.
    auto const bitmap_of_19197 = [] {
        auto values = first_values(19196);
        values.push_back(65535);
        return chain::to_data(filter_of(values));
    }();
    data_chunk bitmap_19196{0xfd, 0xfc, 0x4a};
    bitmap_19196.insert(bitmap_19196.end(), bitmap_of_19197.begin() + 3, bitmap_of_19197.end());

    auto const bitmap = chain::to_data(filter_of(first_values(19197)));
    auto one_more = bitmap;
    one_more.back() = uint8_t(one_more.back() | 0x01);
    data_chunk const short_bitmap(bitmap.begin(), bitmap.end() - 1);
    auto long_bitmap = bitmap;
    long_bitmap.push_back(0x00);

    std::vector<refused> const cases{
        {rice_19197, 4804, "c551e5e24157e4367c8cc57f079c261da9c23a8eb8b70f018a99841b126e453d",
            input_prefix_filter_error::rice_parameter, "Rice coding for 19,197 values"},
        {bitmap_19196, 8196, "9b1c0366745c1b16661d5e317f16ee6d75a1d9ff3699985247e520b8c2ff9c52",
            input_prefix_filter_error::rice_parameter, "a bitmap for 19,196 values"},
        {one_more, 8196, "2586850d51f1817c222f702d38b16670fbe12717088b75bc13fc9fff19176e5b",
            input_prefix_filter_error::bitmap_count, "a bitmap with a value more than the count"},
        {short_bitmap, 8195, "a1a425d9b033a1f3f6e77f3364e07feb1976ca08d6d3c2ae8147c9a27cc084c6",
            input_prefix_filter_error::truncated, "a bitmap a byte short"},
        {long_bitmap, 8197, "f4ebf525e4ef4851cee0e4aef8e29461070b672df51702b2f225d44224bafb3b",
            input_prefix_filter_error::trailing_bytes, "a bitmap a byte long"},
    };
    for (auto const& item : cases) {
        CAPTURE(item.what);
        CHECK(item.bytes.size() == item.size);
        CHECK(encode_base16(bitcoin_hash(item.bytes)) == std::string{item.hash});
        auto const parsed = chain::parse_input_prefix_filter(item.bytes);
        REQUIRE( ! parsed);
        CHECK(parsed.error() == item.error);
    }

    // A bitmap with a value fewer than the count.
    auto one_less = bitmap;
    one_less[4] = uint8_t(one_less[4] & 0x7f);
    auto const fewer = chain::parse_input_prefix_filter(one_less);
    REQUIRE( ! fewer);
    CHECK(fewer.error() == input_prefix_filter_error::bitmap_count);

    // A bitmap cut anywhere is truncated.
    for (size_t size : {size_t(4), size_t(5), size_t(4100), bitmap.size() - 1}) {
        CAPTURE(size);
        auto const cut = chain::parse_input_prefix_filter(byte_span{bitmap.data(), size});
        REQUIRE( ! cut);
        CHECK(cut.error() == input_prefix_filter_error::truncated);
    }
}

TEST_CASE("a prefix of 0 to 16 bits matches a filter with a value under it", "[input prefix filter]") {
    // The values of the block of two transactions.
    auto const filter = filter_of({0x0c06, 0x231f, 0x5e33, 0xa8e6, 0xc535});
    struct probe {
        uint16_t prefix;
        uint8_t bits;
        bool found;
    };
    std::vector<probe> const probes{
        {0x231f, 16, true}, {0x2320, 16, false}, {0x0c06, 16, true}, {0x0000, 16, false}, {0xffff, 16, false},
        {0x231, 12, true}, {0x232, 12, false}, {0xc53, 12, true}, {0xc54, 12, false},
        {0x23, 8, true}, {0x24, 8, false}, {0xa8, 8, true}, {0xff, 8, false},
        {0x0, 4, true}, {0x2, 4, true}, {0x3, 4, false}, {0xa, 4, true}, {0xb, 4, false}, {0xc, 4, true},
        {0xd, 4, false}, {0xf, 4, false},
        {0x0, 0, true},
    };
    for (auto const& item : probes) {
        CAPTURE(item.prefix, item.bits);
        auto const found = chain::has_input_prefix(filter, item.prefix, item.bits);
        REQUIRE(found);
        CHECK(*found == item.found);
    }

    // The first and the last value, at the ends of their ranges.
    auto const ends = filter_of({0x0000, 0xffff});
    CHECK(chain::has_input_prefix(ends, 0x0, 4) == true);
    CHECK(chain::has_input_prefix(ends, 0xf, 4) == true);
    CHECK(chain::has_input_prefix(ends, 0xffff, 16) == true);
    CHECK(chain::has_input_prefix(ends, 0xfffe, 16) == false);
    CHECK(chain::has_input_prefix(ends, 0x0001, 16) == false);
    // No values: nothing matches, not even the empty prefix.
    CHECK(chain::has_input_prefix(filter_of({}), 0, 0) == false);
    CHECK(chain::has_input_prefix(filter_of({}), 0x1234, 16) == false);

    // More than 16 bits, and a bit set past the prefix's length.
    for (auto const& [prefix, bits] : std::vector<std::pair<uint16_t, uint8_t>>{
             {0, 17}, {0x100, 8}, {1, 0}, {0x10, 4}, {0x1000, 12}}) {
        CAPTURE(prefix, bits);
        auto const refused = chain::has_input_prefix(filter, prefix, bits);
        REQUIRE( ! refused);
        CHECK(refused.error() == input_prefix_filter_error::prefix);
    }
}

TEST_CASE("a cfilter carries an input prefix filter as filter type 2", "[input prefix filter]") {
    constexpr auto wire_version = message::version::level::maximum;
    auto const block = block_at(block_vectors()[1]).hash();
    auto const filter = from_hex(block_vectors()[1].filter_hex);
    message::compact_filter const sent{message::input_prefix_compact_filter, block, filter};
    auto const raw = to_data_chunk(sent, wire_version);
    CHECK(encode_base16(raw) == "02" + encode_base16(block) + "05" + std::string{block_vectors()[1].filter_hex});
    auto const received = from_data_chunk<message::compact_filter>(raw, wire_version);
    REQUIRE(received);
    CHECK(*received == sent);
    auto const parsed = chain::parse_input_prefix_filter(received->filter);
    REQUIRE(parsed);
    CHECK(parsed->values == std::vector<uint16_t>{0x9408});
}
