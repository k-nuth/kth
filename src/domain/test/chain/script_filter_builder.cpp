// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

using namespace kth;
using namespace kd;

namespace {

// The published leaf, filter, and header vectors are for this header:
// version 4, a zero parent and a zero merkle root, this timestamp, and zero bits and nonce.
constexpr uint32_t fixture_timestamp = 1293623863;

constexpr std::string_view script_a_hex = "76a914111111111111111111111111111111111111111188ac";
constexpr std::string_view script_b_hex = "76a914333333333333333333333333333333333333333388ac";
constexpr std::string_view script_d_hex = "a914444444444444444444444444444444444444444487";
constexpr std::string_view script_return_hex = "6a04deadbeef";
constexpr std::string_view script_wallet_hex = "a914777777777777777777777777777777777777777787";

constexpr std::string_view leaf_hex[] = {
    "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb010000000000000000",
    "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb020000000000000001",
    "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e010000000100000001",
    "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e020000000000000000",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24010000000000000002",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24010000000000000003",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24020000000000000003",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e000000000000000000",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e010000000000000001",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e010000000100000000",
};

constexpr std::string_view root_of_ten = "f66125233da123c29e93f9510c96557d5edfc071225ea7796c86e6deba8c23fd";
constexpr std::string_view filter_bytes_hex = "0499ff80bec18387496fab00";
constexpr std::string_view served_hex =
    "f66125233da123c29e93f9510c96557d5edfc071225ea7796c86e6deba8c23fd0499ff80bec18387496fab00";
constexpr std::string_view filter_hash_hex = "4ddf591081b2cf7e4ed907dd61b4626d45aa34a1d6c34ef3d475e97b4fc1745f";
constexpr std::string_view header_zero_hex = "a92dc54cdf66fdf505a4c7acdb432becbc5f2756fb3ba0bc0fa74694888f6829";
constexpr std::string_view header_counting_hex = "1a234486b2eec2c3c45e871a2ccb3f12ad9e8a0755fc1a42e6dffbebd5f09f26";
constexpr std::string_view key_a_hex = "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e";
constexpr std::string_view key_c_hex = "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24";

struct fixture {
    chain::block block;
    std::vector<chain::output> spent;
};

void require_hex(byte_span bytes, std::string_view hex) {
    REQUIRE(encode_base16(bytes) == std::string{hex});
}

chain::script script_from_hex(std::string_view hex) {
    auto decoded = decode_base16(hex);
    REQUIRE(decoded);
    return chain::script{std::move(*decoded), false};
}

hash_digest category_c() {
    hash_digest category{};
    category.fill(0x22);
    return category;
}

hash_digest leading_byte(uint8_t first) {
    hash_digest hash{};
    hash.front() = first;
    return hash;
}

chain::input make_input(hash_digest const& hash, uint32_t index, chain::script script) {
    return chain::input{chain::output_point{hash, index}, std::move(script), max_input_sequence};
}

chain::output make_output(uint64_t value, chain::script script, chain::token_data_opt token = {}) {
    return chain::output{value, std::move(script), std::move(token)};
}

chain::token_data_opt fungible(uint64_t amount) {
    return chain::token_data_opt{chain::make_fungible(category_c(), amount)};
}

// The filter key is the leading 16 bytes of the hash in stored order.
chain::golomb_key filter_key(hash_digest const& hash) {
    chain::golomb_key key{};
    std::copy_n(hash.begin(), key.size(), key.begin());
    return key;
}

fixture script_filter_fixture() {
    auto const coinbase = chain::transaction{
        1,
        0,
        chain::input::list{make_input(null_hash, chain::point::null_index, script_from_hex("51"))},
        chain::output::list{
            make_output(5000, script_from_hex(script_a_hex)),
            make_output(0, script_from_hex(script_return_hex)),
            make_output(0, chain::script{}),
        }};

    auto const spent_hash = leading_byte(0x99);
    auto const tokens = chain::transaction{
        2,
        0,
        chain::input::list{
            make_input(spent_hash, 0, chain::script{}),
            make_input(spent_hash, 1, chain::script{}),
        },
        chain::output::list{
            make_output(1000, script_from_hex(script_d_hex), fungible(40)),
            make_output(2000, script_from_hex(script_a_hex)),
        }};

    auto const last = chain::transaction{
        2,
        0,
        chain::input::list{make_input(spent_hash, 2, chain::script{})},
        chain::output::list{make_output(900, script_from_hex(script_b_hex))}};

    chain::header const header{4, null_hash, null_hash, fixture_timestamp, 0, 0};
    return fixture{
        chain::block{header, chain::transaction::list{coinbase, tokens, last}},
        chain::output::list{
            make_output(0, script_from_hex(script_a_hex), fungible(100)),
            make_output(0, script_from_hex(script_b_hex)),
            make_output(0, script_from_hex(script_d_hex), fungible(40)),
        }};
}

} // namespace

// Start Test Suite: script filter builder tests

TEST_CASE("script leaves of the test block are the ten known leaves", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);
    REQUIRE(leaves->size() == std::size(leaf_hex));
    for (size_t i = 0; i < std::size(leaf_hex); ++i) {
        require_hex(chain::to_data((*leaves)[i]), leaf_hex[i]);
    }

    require_hex(chain::script_key(script_from_hex(script_a_hex).bytes()), key_a_hex);
    require_hex(chain::category_key(category_c()), key_c_hex);
}

TEST_CASE("roots of the test block leaf prefixes match the known roots", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);

    struct prefix_root {
        size_t count;
        std::string_view root;
    };
    prefix_root const roots[] = {
        {0, "0000000000000000000000000000000000000000000000000000000000000000"},
        {1, "9d1f540224023621706bc5c9a4cb19c21abb295d62aebaa75137bbcb44dadfb5"},
        {2, "e5c9cff033bb91a3f42a868728b860c8a4eb1e6047d483b7b3a066f694f2180e"},
        {3, "bbf4f9772400cd1355cfd4c39fb37e5f03a63e2452b1ca97deba45d475771dc2"},
        {5, "378bce73f320bd2b71796e5731f6e91d71e42ecf0513d1912f0e9439fb52a2b9"},
        {10, root_of_ten},
    };
    for (auto const& item : roots) {
        auto const prefix = std::span<chain::script_leaf const>{leaves->data(), item.count};
        require_hex(chain::script_leaves_root(prefix), item.root);
    }

    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    require_hex(filter->root, root_of_ten);
    REQUIRE(filter->root == chain::script_leaves_root(*leaves));
}

TEST_CASE("the test block filter matches its four keys and nothing else", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);
    REQUIRE(filter->root == chain::script_leaves_root(*leaves));
    REQUIRE(filter->set.size() == 4u);

    auto const key = filter_key(sample.block.hash());
    hash_digest const members[] = {
        chain::script_key(script_from_hex(script_a_hex).bytes()),
        chain::script_key(script_from_hex(script_b_hex).bytes()),
        chain::category_key(category_c()),
        chain::script_key(script_from_hex(script_d_hex).bytes()),
    };
    for (auto const& member : members) {
        REQUIRE(filter->set.match(key, member));
    }

    auto const op_return = chain::script_key(script_from_hex(script_return_hex).bytes());
    auto const empty = chain::script_key(byte_span{});
    auto const& prev = sample.block.transactions()[1].inputs()[0].previous_output();
    data_chunk outpoint(chain::point::satoshi_fixed_size());
    byte_writer writer(outpoint);
    REQUIRE(prev.to_data(writer, true));
    auto const raw_a = script_from_hex(script_a_hex).bytes();

    REQUIRE( ! filter->set.match(key, op_return));
    REQUIRE( ! filter->set.match(key, empty));
    REQUIRE( ! filter->set.match(key, outpoint));
    REQUIRE( ! filter->set.match(key, raw_a));
}

TEST_CASE("a block that only spends a script still matches that script", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const wallet = script_from_hex(script_wallet_hex);
    auto const spender = chain::transaction{
        2,
        0,
        chain::input::list{make_input(leading_byte(0x99), 7, chain::script{})},
        chain::output::list{make_output(100, script_from_hex(script_b_hex))}};
    chain::block const only{
        sample.block.header(),
        chain::transaction::list{sample.block.transactions()[0], spender}};
    chain::output::list const spent{make_output(0, wallet)};

    auto const filter = chain::build_script_filter(only, spent);
    REQUIRE(filter);
    auto const wallet_key = chain::script_key(wallet.bytes());
    REQUIRE(filter->set.match(filter_key(only.hash()), wallet_key));
}

TEST_CASE("too few or too many spent outputs are refused", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const check = [&](std::span<chain::output const> spent) {
        auto const leaves = chain::script_leaves(sample.block, spent);
        REQUIRE( ! leaves);
        REQUIRE(leaves.error() == chain::script_filter_error::spent_outputs);
        auto const filter = chain::build_script_filter(sample.block, spent);
        REQUIRE( ! filter);
        REQUIRE(filter.error() == chain::script_filter_error::spent_outputs);
    };

    check(std::span<chain::output const>{sample.spent.data(), 2});
    auto extra = sample.spent;
    extra.push_back(make_output(0, script_from_hex(script_a_hex)));
    check(extra);
    check({});
}

TEST_CASE("served bytes, filter hash, and headers of the test block match the known values", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    require_hex(filter->set.to_data(), filter_bytes_hex);

    auto const served = chain::to_data(*filter);
    require_hex(served, served_hex);
    require_hex(chain::script_filter_hash(*filter), filter_hash_hex);

    require_hex(chain::script_filter_header(*filter, null_hash), header_zero_hex);

    hash_digest counting{};
    for (size_t i = 0; i < counting.size(); ++i) {
        counting[i] = static_cast<uint8_t>(i);
    }
    require_hex(chain::script_filter_header(*filter, counting), header_counting_hex);

    chain::script_filter const other{filter->set, null_hash};
    REQUIRE(encode_base16(chain::script_filter_header(other, null_hash)) != std::string{header_zero_hex});
}

TEST_CASE("a served script filter parses back and a short buffer is refused", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    auto const served = chain::to_data(*filter);

    auto const parsed = chain::parse_script_filter(served);
    REQUIRE(parsed);
    REQUIRE(parsed->root == filter->root);
    REQUIRE(parsed->set.size() == filter->set.size());
    auto const key_a = chain::script_key(script_from_hex(script_a_hex).bytes());
    REQUIRE(parsed->set.match(filter_key(sample.block.hash()), key_a));

    auto const empty = chain::parse_script_filter({});
    REQUIRE( ! empty);
    REQUIRE(empty.error() == chain::script_filter_error::too_short);

    auto const short_root = chain::parse_script_filter(byte_span{served.data(), 31});
    REQUIRE( ! short_root);
    REQUIRE(short_root.error() == chain::script_filter_error::too_short);

    // A root with no set bytes is long enough to hold the root, and the set parser refuses the empty remainder.
    data_chunk const root_only(hash_size, 0);
    auto const no_set = chain::parse_script_filter(root_only);
    REQUIRE( ! no_set);
    REQUIRE(no_set.error() == chain::script_filter_error::set);
}

TEST_CASE("the script filter of the test block's loose transactions matches their keys", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const txs = std::span<chain::transaction const>{
        sample.block.transactions().data() + 1,
        sample.block.transactions().size() - 1};
    auto const hash = leading_byte(0x42);

    auto const filter = chain::build_tx_script_filter(hash, txs, sample.spent);
    REQUIRE(filter);

    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);
    std::vector<data_chunk> wanted;
    for (auto const& leaf : *leaves) {
        if (leaf.tx_index > 0) {
            wanted.push_back(to_chunk(leaf.key));
        }
    }
    auto distinct = wanted;
    std::ranges::sort(distinct);
    auto const unique_tail = std::ranges::unique(distinct);
    distinct.erase(unique_tail.begin(), distinct.end());

    auto const key = filter_key(hash);
    auto const want = chain::golomb_set::build(key, wanted);
    REQUIRE(want);
    REQUIRE(filter->to_data() == want->to_data());
    REQUIRE(filter->size() == static_cast<uint32_t>(distinct.size()));
    for (auto const& item : distinct) {
        REQUIRE(filter->match(key, item));
    }

    uint8_t const other_script[] = {0x51, 0x52};
    REQUIRE( ! filter->match(key, chain::script_key(byte_span{other_script})));

    auto const too_few = std::span<chain::output const>{sample.spent.data(), 2};
    auto const few = chain::build_tx_script_filter(hash, txs, too_few);
    REQUIRE( ! few);
    REQUIRE(few.error() == chain::script_filter_error::spent_outputs);

    auto too_many = sample.spent;
    too_many.push_back(make_output(0, chain::script{}));
    auto const many = chain::build_tx_script_filter(hash, txs, too_many);
    REQUIRE( ! many);
    REQUIRE(many.error() == chain::script_filter_error::spent_outputs);

    auto const burn = chain::transaction{
        2,
        0,
        chain::input::list{make_input(leading_byte(0x07), 0, chain::script{})},
        chain::output::list{make_output(0, script_from_hex("6a"))}};
    chain::output::list const burned{make_output(0, chain::script{})};
    auto const empty = chain::build_tx_script_filter(hash, std::span<chain::transaction const>{&burn, 1}, burned);
    REQUIRE(empty);
    REQUIRE(empty->size() == 0u);
}

TEST_CASE("served bytes of the test block are the known root followed by the known filter", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    require_hex(chain::to_data(*filter), served_hex);
    require_hex(filter->set.to_data(), filter_bytes_hex);
    require_hex(filter->root, root_of_ten);
}
