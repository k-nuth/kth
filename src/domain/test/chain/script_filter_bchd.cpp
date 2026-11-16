// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <kth/domain/chain/script_filter_builder.hpp>

using namespace kth;
using namespace kd;

namespace {

// Blocks of a simnet chain made on a bchd node, each with the scripts of the
// outputs it spends, in input order, the filter header of its parent, and the
// script filter and filter header the Go implementation of filter type 1 for
// bchd builds for it, transaction leaves included. A node of another
// implementation that rebuilds the same bytes from the same block serves the
// same light wallets.
//
// Block 125 is not among them, so the filter header before block 126 is a
// given input; each later one chains from the header of the block before.
//
// The blocks: the genesis block; a payment to three scripts, a script hash and
// a data carrier, with change; an output created and spent in the same block;
// the spend of an output of an earlier block, with one script paid twice and a
// script paid before paid again; and a block with a coinbase alone.
struct node_vector {
    uint32_t height;
    std::string_view block_hex;
    std::vector<std::string_view> spent_script_hex;
    std::string_view parent_header_hex;
    std::string_view served_hex;
    std::string_view header_hex;
};

std::vector<node_vector> const& node_vectors() {
    static std::vector<node_vector> const vectors{
    {
        0,
        "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e"
        "67768f617fc81bc3888a51323a9fb8aa4b1e5e4a45068653ffff7f200200000001010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f"
        "4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f75742066"
        "6f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a6"
        "7962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
        {},
        "0000000000000000000000000000000000000000000000000000000000000000",
        "c2da4de436a49fc717279422a4e6f7047bb384c006c1ac38bd54f7f95ed43f4502602548242a00",
        "15f81de96b9c966f49c191932510c835ea2e7e150079892abd22ffc6751f1748",
    },
    {
        126,
        "01000030c291371279ddaecb5af77e92580dda04af35e32af6fd654c839dd37ffa8cd54ea837207273480cd114d8cdc6"
        "549ab5dd115cf8f7aaf889e6c13be719c7fb519c91b0f66affff7f200100000002010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff12017e087bd5c70f2505afa9062f626368642fffffff"
        "ff01b2fe052a010000001976a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac00000000010000000160c931"
        "10e396184a1eb4b86ef3af0ac41918193bb1b31171cd841ebbf63e96d1000000006441b8f9c07a7af38efdc51783b98d"
        "43a661c0cb801bd11a62468181870bc605e4da6881d2b8131b2ae93e9fb8db1a761d0d96f02737c691c6725a65aa0811"
        "fc8186412102be31cab636041636a1846f4358a241f608ca9d9f29177585280590f9d97e033fffffffff0600e1f50500"
        "0000001976a914010101010101010101010101010101010101010188ac00e1f505000000001976a91402020202020202"
        "0202020202020202020202020288ac00e1f505000000001976a914030303030303030303030303030303030303030388"
        "ac00c2eb0b0000000017a914da1745e9b549bd0bfa1a569971c77eba30cd5a4b870000000000000000176a1573637269"
        "70742066696c74657220766563746f72734e80380c010000001976a9149ac056c1f3ebbf9b93995d3ce4fa08c7628c3c"
        "8b88ac00000000",
        {"76a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac"},
        "ca783e12db56d2e1642716b5562344aec0eaa0be071afbe6a32d87989d05f164",
        "3b7c45a5279d29547ac2755d20a3d0cccd514ec80628f71b2844120d2a4648b508e2ee947e15ab48b82928b84811bfd96b"
        "ea8d54467c",
        "3362679fcffacc8c327f41e9e79b3818bcca4f2359438ca3270350ea39b64ad1",
    },
    {
        127,
        "01000030faec54d16b2a5bdbc3371f5e1593a8166e95a55d0df4f8ee33b6d8281bee6d79816c95ada87beb3bac7be563"
        "31697aa815d04915e94c1d6570812f083e9669f791b0f66affff7f200000000003010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff12017f086f88e3cf77f9ac8d062f626368642fffffff"
        "ff01c621062a010000001976a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac00000000010000000178618f"
        "d7cc15e6088f2b77cc5ac25dd92ad86619bbe91d693427a9cd965e8708000000006441dfea2c0f1c08e147bdf45182ca"
        "31e99745704f5caa7f348d3b9e55ff01a594141bf235e38ad141d4389033312c576917f0652c754bb452127a069a3dfa"
        "b511d9412102be31cab636041636a1846f4358a241f608ca9d9f29177585280590f9d97e033fffffffff0300a3e11100"
        "00000017a91469d7ef8f42a25e8791bb37d5fb48456f102a3cb9870000000000000000176a157363726970742066696c"
        "74657220766563746f72734a462418010000001976a91438b726cdbeb1d7f311544519c3412a4ad67c26fb88ac000000"
        "0002000000013a90cdbcad008793ceca6cc7d7dd73c22e1d85dd0858760234cae2c1c79fdf2100000000020152ffffff"
        "ff02f07be111000000001976a914040404040404040404040404040404040404040488ac0000000000000000176a1573"
        "63726970742066696c74657220766563746f727300000000",
        {"76a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac", "a91469d7ef8f42a25e8791bb37d5fb48456f102a3cb987"},
        "3362679fcffacc8c327f41e9e79b3818bcca4f2359438ca3270350ea39b64ad1",
        "0203471b91863237e77edce7baf8aece3399215b146501a2d5da70a0cc33819107e621fae1832906b2f0e54a00d93660"
        "c5896e",
        "e5058dd1d7d008d1391eec4ec1abc95748c837760f02c0157b4b6357d925839b",
    },
    {
        128,
        "01000030686855ab00ec43dfebf35fefed20d6d8123f8a6f8627b58568f5ed1269465f0f12264c20395d784802565eaa"
        "ac3822684d5c6693c94ac5c3c92652690c9efac892b0f66affff7f200000000002010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff1302800008048ef567f6d58a7d062f626368642fffff"
        "ffff011019062a010000001976a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac0000000002000000010299"
        "b0b714907271b4e2a90924bb1fb24eb940e6dc14419af3a68b145572c40703000000020151ffffffff0480f0fa020000"
        "00001976a914050505050505050505050505050505050505050588ac80f0fa02000000001976a9140505050505050505"
        "05050505050505050505050588acf0b9f505000000001976a914010101010101010101010101010101010101010188ac"
        "0000000000000000176a157363726970742066696c74657220766563746f727300000000",
        {"a914da1745e9b549bd0bfa1a569971c77eba30cd5a4b87"},
        "e5058dd1d7d008d1391eec4ec1abc95748c837760f02c0157b4b6357d925839b",
        "d8dc712cae1f62a1a0e166f56489ebde33ff6acc598ab911749a451357e68bd80633cc5b7f4d5913400657bb47dda926"
        "90",
        "f395f6b7e31dc21b365269a24c1008cf758764062923b1a952719dd73a62f7a7",
    },
    {
        129,
        "0100003001df74d1cc4218b05ddc749c6961642bb240acf79a333a16b9ffd9a6f9e6e62a03178670cd502a4f6f1a6fc8"
        "23a580369e03e9e936adc9aed0db40df7975c65e92b0f66affff7f200100000001010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff13028100083ab923e718789d91062f626368642fffff"
        "ffff0100f2052a010000001976a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac00000000",
        {},
        "f395f6b7e31dc21b365269a24c1008cf758764062923b1a952719dd73a62f7a7",
        "b4512eae7cb64b3b42807855f58b600362ebd292d59dabe1c4d92d26eb2e46490206bedc0d0680",
        "4cd8c557a61b1ef8c7de3bfcfbb037e3e53632303a4e6bfac7e6067f853c0197",
    },
    };
    return vectors;
}

data_chunk from_hex(std::string_view hex) {
    auto decoded = decode_base16(hex);
    REQUIRE(decoded);
    return std::move(*decoded);
}

hash_digest hash_from_hex(std::string_view hex) {
    auto const bytes = from_hex(hex);
    REQUIRE(bytes.size() == hash_size);
    hash_digest hash{};
    std::copy(bytes.begin(), bytes.end(), hash.begin());
    return hash;
}

// The block of the vector at `height`, and the outputs it spends.
struct node_block {
    chain::block block;
    std::vector<chain::output> spent;
};

node_block node_block_at(uint32_t height) {
    auto const found = std::ranges::find(node_vectors(), height, &node_vector::height);
    REQUIRE(found != node_vectors().end());
    auto const raw = from_hex(found->block_hex);
    byte_reader reader(raw);
    auto block = chain::block::from_data(reader);
    REQUIRE(block);
    std::vector<chain::output> spent;
    for (auto const script_hex : found->spent_script_hex) {
        spent.push_back(chain::output{0, chain::script{from_hex(script_hex), false}, chain::token_data_opt{}});
    }
    return {std::move(*block), std::move(spent)};
}

} // namespace

TEST_CASE("script filter of blocks from another node's chain is the filter and header the Go implementation builds", "[script filter builder]") {
    REQUIRE(node_vectors().size() == 5);

    for (auto const& vector : node_vectors()) {
        CAPTURE(vector.height);

        auto const raw = from_hex(vector.block_hex);
        byte_reader reader(raw);
        auto const block = chain::block::from_data(reader);
        REQUIRE(block);

        std::vector<chain::output> spent;
        for (auto const script_hex : vector.spent_script_hex) {
            spent.push_back(chain::output{0, chain::script{from_hex(script_hex), false}, chain::token_data_opt{}});
        }

        auto const filter = chain::build_script_filter(*block, spent);
        REQUIRE(filter);
        REQUIRE(encode_base16(chain::to_data(*filter)) == std::string{vector.served_hex});

        auto const header = chain::script_filter_header(*filter, hash_from_hex(vector.parent_header_hex));
        REQUIRE(encode_base16(header) == std::string{vector.header_hex});
    }
}

TEST_CASE("script filter of another node's chain parses and matches the scripts its block spends and its transactions", "[script filter builder]") {
    for (auto const& vector : node_vectors()) {
        CAPTURE(vector.height);

        auto const raw = from_hex(vector.block_hex);
        byte_reader reader(raw);
        auto const block = chain::block::from_data(reader);
        REQUIRE(block);

        auto const parsed = chain::parse_script_filter(from_hex(vector.served_hex));
        REQUIRE(parsed);

        // The filter is keyed by the first bytes of the block hash.
        chain::golomb_key key{};
        auto const hash = block->hash();
        std::copy_n(hash.begin(), key.size(), key.begin());

        for (auto const script_hex : vector.spent_script_hex) {
            auto const script = from_hex(script_hex);
            REQUIRE(parsed->set.match(key, chain::script_key(script)));
        }

        // Every transaction of the block, the coinbase included, by its hash alone.
        for (auto const& tx : block->transactions()) {
            REQUIRE(parsed->set.match(key, chain::transaction_key(tx.hash())));
        }

        // A script no block of the chain has is in none of them.
        data_chunk const absent{0x76, 0xa9, 0x14, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0x88, 0xac};
        REQUIRE( ! parsed->set.match(key, chain::script_key(absent)));
    }
}

TEST_CASE("a whole txid key in a block of another node's chain gives the run and edge hashes the Go implementation gives", "[script filter builder]") {
    auto const sample = node_block_at(127);
    auto const leaves = chain::script_leaves(sample.block, sample.spent);
    REQUIRE(leaves);
    std::span<chain::script_leaf const> const all = *leaves;
    REQUIRE(all.size() == 9u);
    auto const root = chain::script_leaves_root(all);
    REQUIRE(encode_base16(root) == "0203471b91863237e77edce7baf8aece3399215b146501a2d5da70a0cc338191");

    // The key, the first leaf of the run, how many leaves it has, and the edge
    // hashes of the run with the leaf before and the leaf after it.
    struct txid_range {
        std::string_view key;
        size_t first;
        size_t count;
        std::vector<std::string_view> hashes;
    };
    std::vector<txid_range> const ranges{
        {"003fb0bb55849986a01b708caf5f7096cb333b2db4c652142d266e9af0ef39d8", 0, 1, {
            "fc7106ec8a756b1c6d302c0ad0d80d9386902883f246bb14b22eb831e8e24b55",
            "57e6982497f1437070b821d7af36a7a965865c6f11e749efc55c4871893623f0",
            "f7ac8e989fe8a5b97ac6a4ae695e6218ffa066b89b1a0cc7b77a0b214200d204"}},
        {"2c85f8a20f23fef92f6c796431dfed75e32ec742046f561594cecff21ac84ee3", 1, 1, {
            "70cc85d12c594282065f0bd3d401fbbc52ba2fe5f89f85c0c3910bd82c1a7237",
            "57e6982497f1437070b821d7af36a7a965865c6f11e749efc55c4871893623f0",
            "f7ac8e989fe8a5b97ac6a4ae695e6218ffa066b89b1a0cc7b77a0b214200d204"}},
        {"a625efaa008d78b7008537a5ffe4a50b269f2233227d76f2f0b10328cc5975f8", 5, 1, {
            "3276e211a8051a12e287fa3f5887fae9c23ed0e5680d888b0281bd34d123e8a1",
            "7027d50c343da427699d99a1e39bc51ded643a37463ff6e5ccfe435367bf1556",
            "f7ac8e989fe8a5b97ac6a4ae695e6218ffa066b89b1a0cc7b77a0b214200d204"}},
        // A transaction of block 128: no leaf, and the neighbours show it.
        {"ed57fe6068f159d30f2371fa2d30bf341db9118396650c28e94e7a8721ce4257", 6, 0, {
            "6319d35dfb691c1b7b845ee21606fc12f427dd1810913b50b9b03109528ddc88",
            "3276e211a8051a12e287fa3f5887fae9c23ed0e5680d888b0281bd34d123e8a1",
            "7027d50c343da427699d99a1e39bc51ded643a37463ff6e5ccfe435367bf1556",
            "f7ac8e989fe8a5b97ac6a4ae695e6218ffa066b89b1a0cc7b77a0b214200d204"}},
    };

    auto const& transactions = sample.block.transactions();
    REQUIRE(transactions.size() == 3);
    for (size_t i = 0; i < transactions.size(); ++i) {
        REQUIRE(chain::transaction_key(transactions[i].hash()) == hash_from_hex(ranges[i].key));
    }
    REQUIRE(chain::transaction_key(node_block_at(128).block.transactions()[1].hash()) == hash_from_hex(ranges[3].key));

    for (size_t r = 0; r < ranges.size(); ++r) {
        auto const& item = ranges[r];
        CAPTURE(item.key);
        auto const key = hash_from_hex(item.key);
        auto const run = chain::script_leaves_with_prefix(all, key, 256);
        REQUIRE(run);
        REQUIRE(run->first == item.first);
        REQUIRE(run->end - run->first == item.count);
        if (item.count == 1) {
            chain::script_leaf const wanted{key, static_cast<uint32_t>(r), 0, chain::script_leaf_kind::transaction};
            REQUIRE(all[run->first] == wanted);
        }

        auto const lo = run->first == 0 ? run->first : run->first - 1;
        auto const hi = run->end == all.size() ? run->end : run->end + 1;
        auto const hashes = chain::script_range_hashes(all, lo, hi);
        REQUIRE(hashes);
        REQUIRE(hashes->size() == item.hashes.size());
        for (size_t i = 0; i < item.hashes.size(); ++i) {
            REQUIRE(encode_base16((*hashes)[i]) == std::string{item.hashes[i]});
        }
        auto const* before = run->first == 0 ? nullptr : &all[run->first - 1];
        auto const* after = run->end == all.size() ? nullptr : &all[run->end];
        REQUIRE(chain::check_script_range(root, static_cast<uint32_t>(all.size()), static_cast<uint32_t>(run->first),
            all.subspan(run->first, item.count), before, after, *hashes, key, 256));
    }
}

TEST_CASE("loose transactions of a block of another node's chain make the filter the Go implementation builds", "[script filter builder]") {
    // The two transactions after the coinbase of block 127, keyed by the zero
    // hash: the second spends an output of the first.
    auto const sample = node_block_at(127);
    auto const& transactions = sample.block.transactions();
    auto const loose = std::span<chain::transaction const>{transactions.data() + 1, transactions.size() - 1};
    auto const filter = chain::build_tx_script_filter(null_hash, loose, sample.spent);
    REQUIRE(filter);
    REQUIRE(filter->size() == 6u);
    REQUIRE(encode_base16(filter->to_data()) == "0627b5e1afe36a5690382e78663605ce");
}
