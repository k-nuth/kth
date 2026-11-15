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
// script filter and filter header that node serves for it (`getcfilter` and
// `getcfilterheader` with filter type 1). A node of another implementation
// that rebuilds the same bytes from the same block serves the same light
// wallets.
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
        "904b96e24a5b5bb8b488ab0b8699406a89cd0ee8b9ec9d7a2711b5ab517e13f5013012a0",
        "2907e70bbbb30da593e761451a1f4eb1cd630b1e0da62ed0272f3f0423a61edc",
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
        "8ad04c9491de79a7dd3f3d4b7163f83a3c1a4406fc172076dc877345ef0bd20b06cc65ddcbf1c3dbd28d834bf8c4bbfa"
        "80",
        "f0380fbcf33c6b758b6bdc19294600cde5ac96eae7894146c8dddfba372d2e70",
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
        "f0380fbcf33c6b758b6bdc19294600cde5ac96eae7894146c8dddfba372d2e70",
        "96abbd5fe8f576296a2f50b3ebe8f03d61e2dba7968309a18d6ceab2be7861db04bbbb62037514a1956b1740",
        "ad3062be24cf55c6ef4a95cdc08df52a30aa14b922a2d52e055730b5bfb0d36f",
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
        "ad3062be24cf55c6ef4a95cdc08df52a30aa14b922a2d52e055730b5bfb0d36f",
        "d8b0a925f1c1550536bcd77d3a1b02768245badf91acb5c2ff43591152ad6b930422883ce091c043a77cdb40",
        "4e57c5432d961c613358d6754b396a0fb43582c76981278527bc976c69df31ab",
    },
    {
        129,
        "0100003001df74d1cc4218b05ddc749c6961642bb240acf79a333a16b9ffd9a6f9e6e62a03178670cd502a4f6f1a6fc8"
        "23a580369e03e9e936adc9aed0db40df7975c65e92b0f66affff7f200100000001010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff13028100083ab923e718789d91062f626368642fffff"
        "ffff0100f2052a010000001976a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac00000000",
        {},
        "4e57c5432d961c613358d6754b396a0fb43582c76981278527bc976c69df31ab",
        "70cc85d12c594282065f0bd3d401fbbc52ba2fe5f89f85c0c3910bd82c1a723701828018",
        "98487e23d978d3347e71f381d17aedc76376df1a899a486cdea8e10de9584a0a",
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

} // namespace

TEST_CASE("script filter of blocks from another node is the filter and header that node serves", "[script filter builder]") {
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

TEST_CASE("script filter served by another node parses and matches the scripts its block spends", "[script filter builder]") {
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

        // A script no block of the chain has is in none of them.
        data_chunk const absent{0x76, 0xa9, 0x14, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0x88, 0xac};
        REQUIRE( ! parsed->set.match(key, chain::script_key(absent)));
    }
}
