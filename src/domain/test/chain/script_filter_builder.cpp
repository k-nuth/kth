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

// Three of them are transaction leaves, one for each transaction: the
// coinbase's at index 2, the third transaction's at 3 and the second's at 9.
constexpr std::string_view leaf_hex[] = {
    "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb010000000000000000",
    "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb020000000000000001",
    "2408f309be44080bfa3bb2af14c6725ed1f8f4e46f1af24b4be3488061398758000000000000000004",
    "29793475c422842053b828ed76beb29702bcd1aefffa403ae6c3762f04921716020000000000000004",
    "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e010000000100000001",
    "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e020000000000000000",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24010000000000000002",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24010000000000000003",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24020000000000000003",
    "6b1601808de128a10737cdea9a192ad5e5e47d37cf06349b37aa57f968f2e39f010000000000000004",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e000000000000000000",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e010000000000000001",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e010000000100000000",
};

constexpr std::string_view root_of_leaves = "b0b1eb8ae0f0a27876a014e84906f626d6b77028c8197763cb4f6c13b5041d3d";
constexpr std::string_view filter_bytes_hex = "07cebf94a6e9031661043903d7cd355e4ee520";
constexpr std::string_view served_hex =
    "b0b1eb8ae0f0a27876a014e84906f626d6b77028c8197763cb4f6c13b5041d3d07cebf94a6e9031661043903d7cd355e4ee520";
constexpr std::string_view filter_hash_hex = "efda295187e26a5cc26e977352d97d8b74e1a59bcd82f3396521ffd9ab9b5444";
constexpr std::string_view header_zero_hex = "31bf06e3a313b4a69ae63d7065c4f261dbe76f498d62c1fa5f1c40e50c976af4";
constexpr std::string_view header_counting_hex = "3b5c87c739ea1dca7f2c253c9b2ad3b1fc09644eb2f0e833482e93a091e9ac13";
constexpr std::string_view key_a_hex = "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e";
constexpr std::string_view key_c_hex = "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24";

// The hash of each transaction of the test block, in the order the hash
// function writes it, and the key of its transaction leaf.
constexpr std::string_view txid_hex[] = {
    "3d3eaa7cae56fc7823601f845a53018ae8febbe77f0a6ffcdbac2794638c60eb",
    "bdbfe452ac34ce73c03efb1865a78bafa4b10cc55ded3a24f386e23001c2a8b0",
    "d5d1622cfc9d6c9fd27dfa7c3255a15f4ba58adb1907310f26da4a8fe7f14d59",
};
constexpr std::string_view txid_key_hex[] = {
    "2408f309be44080bfa3bb2af14c6725ed1f8f4e46f1af24b4be3488061398758",
    "6b1601808de128a10737cdea9a192ad5e5e47d37cf06349b37aa57f968f2e39f",
    "29793475c422842053b828ed76beb29702bcd1aefffa403ae6c3762f04921716",
};

// The hash of the genesis block's coinbase as it is stored (displayed
// 4a5e1e4b...a33b), and the transaction key, category key and script key of
// those 32 bytes.
constexpr std::string_view genesis_txid_hex = "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a";
constexpr std::string_view genesis_txid_key_hex = "025ed1aacae75dea93415773bb9a23b3942e03ef36aebc59acb67406902d03ce";
constexpr std::string_view genesis_category_key_hex = "14b1e7df36fc97e5ded3b7fabcbcc38dbdec513c5f96e4d4bf42107d26699935";
constexpr std::string_view genesis_script_key_hex = "337e2a4de220e4518f2a8a4bd8bc2c82b0b92cea3bb2ab9fceda68e1738e0b34";

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

hash_digest hash_from_hex(std::string_view hex) {
    auto const decoded = decode_base16<hash_size>(hex);
    REQUIRE(decoded);
    return *decoded;
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

TEST_CASE("script leaves of the test block are the thirteen known leaves", "[script filter builder]") {
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

TEST_CASE("transaction key is the SHA-256 of 0x02 and the stored hash, apart from the other keys of those bytes", "[script filter builder]") {
    auto const txid = hash_from_hex(genesis_txid_hex);
    require_hex(chain::transaction_key(txid), genesis_txid_key_hex);
    require_hex(chain::category_key(txid), genesis_category_key_hex);
    require_hex(chain::script_key(txid), genesis_script_key_hex);

    // The display order is the stored hash reversed, and is another key.
    auto reversed = txid;
    std::ranges::reverse(reversed);
    REQUIRE(chain::transaction_key(reversed) != chain::transaction_key(txid));
}

TEST_CASE("every transaction of the test block has one transaction leaf, the coinbase included", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);

    auto const& transactions = sample.block.transactions();
    REQUIRE(transactions.size() == std::size(txid_hex));
    for (size_t i = 0; i < transactions.size(); ++i) {
        INFO("transaction " << i);
        auto const txid = transactions[i].hash();
        require_hex(txid, txid_hex[i]);
        require_hex(chain::transaction_key(txid), txid_key_hex[i]);

        auto const position = static_cast<uint32_t>(i);
        auto const count = std::ranges::count_if(*leaves, [&](chain::script_leaf const& leaf) {
            return leaf.kind == chain::script_leaf_kind::transaction && leaf.tx_index == position;
        });
        REQUIRE(count == 1);
        chain::script_leaf const wanted{chain::transaction_key(txid), position, 0, chain::script_leaf_kind::transaction};
        REQUIRE(std::ranges::find(*leaves, wanted) != leaves->end());
    }
    auto const transaction_leaves = std::ranges::count_if(*leaves, [](chain::script_leaf const& leaf) {
        return leaf.kind == chain::script_leaf_kind::transaction;
    });
    REQUIRE(transaction_leaves == static_cast<std::ptrdiff_t>(transactions.size()));
}

TEST_CASE("the full key of a transaction selects its one leaf and the range proof of that leaf holds", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);
    std::span<chain::script_leaf const> const all = *leaves;
    auto const root = chain::script_leaves_root(all);
    auto const count = static_cast<uint32_t>(all.size());

    // Where each transaction's leaf sits in tree order.
    constexpr size_t places[] = {2, 9, 3};
    auto const& transactions = sample.block.transactions();
    for (size_t i = 0; i < transactions.size(); ++i) {
        INFO("transaction " << i);
        auto const key = chain::transaction_key(transactions[i].hash());
        auto const run = chain::script_leaves_with_prefix(all, key, 256);
        REQUIRE(run);
        REQUIRE(run->first == places[i]);
        REQUIRE(run->end == run->first + 1);
        auto const& found = all[run->first];
        REQUIRE(found.kind == chain::script_leaf_kind::transaction);
        REQUIRE(found.tx_index == static_cast<uint32_t>(i));
        REQUIRE(found.index == 0u);

        // The run with the leaf before and the leaf after it rebuilds the root,
        // and shows the block has no other leaf with that key.
        auto const* before = &all[run->first - 1];
        auto const* after = &all[run->end];
        auto const hashes = chain::script_range_hashes(all, run->first - 1, run->end + 1);
        REQUIRE(hashes);
        REQUIRE(chain::check_script_range(root, count, static_cast<uint32_t>(run->first),
            all.subspan(run->first, 1), before, after, *hashes, key, 256));

        // The same proof does not hold for another transaction's key.
        auto const other = chain::transaction_key(transactions[(i + 1) % transactions.size()].hash());
        REQUIRE( ! chain::check_script_range(root, count, static_cast<uint32_t>(run->first),
            all.subspan(run->first, 1), before, after, *hashes, other, 256));
    }

    // The proof of the coinbase's leaf, as the range in the known vectors.
    auto const coinbase = chain::script_range_hashes(all, 1, 4);
    REQUIRE(coinbase);
    REQUIRE(coinbase->size() == 3u);
    require_hex((*coinbase)[0], "9d1f540224023621706bc5c9a4cb19c21abb295d62aebaa75137bbcb44dadfb5");
    require_hex((*coinbase)[1], "d86f36e990d6b2c6dea6f0e7111c6271963b6a566f70e961a2dc675ab60b1e94");
    require_hex((*coinbase)[2], "e9639e41195ffb0b1f64cdcf9f9f0c318bdcdaa7da3a9b37e2292a5e3d932b7c");
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
        {3, "907cad6618a8f3ede71fa62a7169f055a19ebb073270e1693231429c719be61d"},
        {5, "bbc9efc7bb3eb741dc7ca721578860c688eeb07cdb49ceea332467f70cb2f9a3"},
        {10, "69f2a3d4cdb63540fd67dead5141b47277a37aef1e2636c52e8ef56f25522dfb"},
        {13, root_of_leaves},
    };
    for (auto const& item : roots) {
        auto const prefix = std::span<chain::script_leaf const>{leaves->data(), item.count};
        require_hex(chain::script_leaves_root(prefix), item.root);
    }

    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    require_hex(filter->root, root_of_leaves);
    REQUIRE(filter->root == chain::script_leaves_root(*leaves));
}

TEST_CASE("the test block filter matches its seven keys and nothing else", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);
    REQUIRE(filter->root == chain::script_leaves_root(*leaves));
    REQUIRE(filter->set.size() == 7u);

    auto const key = filter_key(sample.block.hash());
    auto const& transactions = sample.block.transactions();
    hash_digest const members[] = {
        chain::script_key(script_from_hex(script_a_hex).bytes()),
        chain::script_key(script_from_hex(script_b_hex).bytes()),
        chain::category_key(category_c()),
        chain::script_key(script_from_hex(script_d_hex).bytes()),
        chain::transaction_key(transactions[0].hash()),
        chain::transaction_key(transactions[1].hash()),
        chain::transaction_key(transactions[2].hash()),
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

    // A transaction's hash is in the filter only under its tag: not bare, not
    // as a category, not reversed, and not the hash of a transaction the block
    // only spends from.
    auto const coinbase_hash = transactions[0].hash();
    auto reversed = coinbase_hash;
    std::ranges::reverse(reversed);
    REQUIRE( ! filter->set.match(key, coinbase_hash));
    REQUIRE( ! filter->set.match(key, chain::category_key(coinbase_hash)));
    REQUIRE( ! filter->set.match(key, chain::transaction_key(reversed)));
    REQUIRE( ! filter->set.match(key, chain::transaction_key(prev.hash())));
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

    // Each loose transaction has its key, as it has in a block.
    for (auto const& tx : txs) {
        REQUIRE(filter->match(key, chain::transaction_key(tx.hash())));
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
    // A transaction with no script leaf and no category leaf still has its own key.
    chain::output::list const burned{make_output(0, chain::script{})};
    auto const only_hash = chain::build_tx_script_filter(hash, std::span<chain::transaction const>{&burn, 1}, burned);
    REQUIRE(only_hash);
    REQUIRE(only_hash->size() == 1u);
    REQUIRE(only_hash->match(key, chain::transaction_key(burn.hash())));
}

TEST_CASE("served bytes of the test block are the known root followed by the known filter", "[script filter builder]") {
    auto const sample = script_filter_fixture();
    auto const filter = chain::build_script_filter(sample.block, sample.spent);
    REQUIRE(filter);
    require_hex(chain::to_data(*filter), served_hex);
    require_hex(filter->set.to_data(), filter_bytes_hex);
    require_hex(filter->root, root_of_leaves);
}
