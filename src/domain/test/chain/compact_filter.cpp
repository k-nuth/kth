// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/domain/chain/compact_filter.hpp>

using namespace kth;
using namespace kd;

namespace {

// Blocks of a simnet chain made on a bchd node, each with its regular filter
// (type 0), filter hash and filter header over an all-zero previous header, as
// bchd's filter library (gcash/bchutil, gcs/builder) builds them. Hashes are in
// stored byte order. A node that builds the same bytes from the same block
// serves the light wallets that sync from bchd.
//
// The blocks: the genesis block; a payment to three scripts, a script hash and
// a data carrier, with change; an output created and spent in the same block;
// the spend of an output of an earlier block; and a block with a coinbase
// alone.
struct library_vector {
    uint32_t height;
    std::string_view block_hex;
    uint32_t items;
    std::string_view served_hex;
    std::string_view hash_hex;
    std::string_view header_hex;
};

std::vector<library_vector> const& library_vectors() {
    static std::vector<library_vector> const vectors{
    {
        0,
        "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e"
        "67768f617fc81bc3888a51323a9fb8aa4b1e5e4a45068653ffff7f200200000001010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f"
        "4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f75742066"
        "6f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a6"
        "7962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000",
        1,
        "01697c40",
        "a90eba0539a4c316ebdf39394ee2393feec0ce647beb36b9f3ec09225d777ba7",
        "b2ef0f5c5d790832d79fc9c9a7b3cef02dd94f143c63feba9d836248cad624cf",
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
        8,
        "08b3e419dbdd84d1441d82a07994008c4b6cdff9e6f380",
        "d0de7c05e872f61682821200d8f141fc12eb2537ae91ce06e6f2574b3474ab44",
        "79c56a659bd9d872db78c1ccbb8271f43073218d7449c077124b24827fc4d64b",
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
        7,
        "07415a006e6bee1f3520c934743abce01b7f8480",
        "37b0cc8ff7d3e4792b667fa3a2317ee2de7e5d38091a2bcf67d4774b6450899f",
        "325016e7cd3cdc4564feff6f4d36ed13d65b5e4167c40491b47acc4904c7da12",
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
        5,
        "0552f855452bf8cf29860982f7f200",
        "513e4e1447456eeeba5d0153b32f05e576d30d383460bb865209ce0d7b31eccd",
        "b9a2f706a0472b0c1f347fe109c29c84c34f78e30357daf0ebacffbd89994bce",
    },
    {
        129,
        "0100003001df74d1cc4218b05ddc749c6961642bb240acf79a333a16b9ffd9a6f9e6e62a03178670cd502a4f6f1a6fc8"
        "23a580369e03e9e936adc9aed0db40df7975c65e92b0f66affff7f200100000001010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff13028100083ab923e718789d91062f626368642fffff"
        "ffff0100f2052a010000001976a914b2275cc60091cdde77bddb8c132f60494cb2ea6b88ac00000000",
        1,
        "017bdff0",
        "25c7fa3ecde32263ebbaf71c2a92081c01f05a17001a86b471aa1dfaf5c760d3",
        "9e96706a9cd80cdd7b5a1623d01f70a9ed60de8a9a07ce27b1814a4de2f5e60e",
    },
    };
    return vectors;
}

data_chunk from_hex(std::string_view hex) {
    auto decoded = decode_base16(hex);
    REQUIRE(decoded);
    return std::move(*decoded);
}

chain::block block_of(library_vector const& vector) {
    auto const raw = from_hex(vector.block_hex);
    byte_reader reader(raw);
    auto block = chain::block::from_data(reader);
    REQUIRE(block);
    return std::move(*block);
}

chain::script script_of(uint8_t fill) {
    data_chunk bytes{0x76, 0xa9, 0x14};
    bytes.insert(bytes.end(), 20, fill);
    bytes.push_back(0x88);
    bytes.push_back(0xac);
    return chain::script{std::move(bytes), false};
}

data_chunk outpoint_bytes(hash_digest const& hash, uint32_t index) {
    data_chunk bytes(hash.begin(), hash.end());
    bytes.push_back(uint8_t(index & 0xff));
    bytes.push_back(uint8_t((index >> 8) & 0xff));
    bytes.push_back(uint8_t((index >> 16) & 0xff));
    bytes.push_back(uint8_t((index >> 24) & 0xff));
    return bytes;
}

} // namespace

TEST_CASE("regular filter of blocks from another node is the filter that node's library builds", "[compact filter]") {
    REQUIRE(library_vectors().size() == 5);

    for (auto const& vector : library_vectors()) {
        CAPTURE(vector.height);
        auto const block = block_of(vector);

        auto const filter = chain::build_regular_filter(block);
        REQUIRE(filter);
        CHECK(filter->size() == vector.items);
        CHECK(encode_base16(filter->to_data()) == std::string{vector.served_hex});
        CHECK(encode_base16(chain::compact_filter_hash(*filter)) == std::string{vector.hash_hex});
        CHECK(encode_base16(chain::compact_filter_header(*filter, null_hash)) == std::string{vector.header_hex});
    }
}

TEST_CASE("regular filter served by another node parses and matches what its block holds", "[compact filter]") {
    for (auto const& vector : library_vectors()) {
        CAPTURE(vector.height);
        auto const block = block_of(vector);
        auto const key = chain::compact_filter_key(block.hash());

        auto const served = chain::golomb_set::from_data(from_hex(vector.served_hex));
        REQUIRE(served);

        auto const& txs = block.transactions();
        for (size_t i = 0; i < txs.size(); ++i) {
            for (auto const& output : txs[i].outputs()) {
                if ( ! output.script().bytes().empty()) {
                    CHECK(served->match(key, output.script().bytes()));
                }
            }
            if (i == 0) {
                continue;
            }
            for (auto const& input : txs[i].inputs()) {
                auto const& point = input.previous_output();
                CHECK(served->match(key, outpoint_bytes(point.hash(), point.index())));
            }
        }
    }
}

TEST_CASE("regular filter holds input outpoints and output scripts, and nothing of the coinbase input", "[compact filter]") {
    data_chunk one{0x51};
    data_chunk carrier{0x6a, 0x04, 0xde, 0xad, 0xbe, 0xef};
    chain::transaction const coinbase{1, 0,
        chain::input::list{chain::input{chain::output_point{null_hash, chain::point::null_index},
            chain::script{one, false}, max_input_sequence}},
        chain::output::list{
            chain::output{5000, script_of(0x11), chain::token_data_opt{}},
            chain::output{0, chain::script{carrier, false}, chain::token_data_opt{}},
            chain::output{0, chain::script{}, chain::token_data_opt{}},
        }};

    hash_digest funding{};
    funding.fill(0x99);
    chain::transaction const spend{2, 0,
        chain::input::list{
            chain::input{chain::output_point{funding, 0}, chain::script{}, max_input_sequence},
            chain::input{chain::output_point{funding, 7}, chain::script{}, max_input_sequence},
        },
        chain::output::list{
            chain::output{100, script_of(0x22), chain::token_data_opt{}},
            // Paid twice: one item.
            chain::output{200, script_of(0x11), chain::token_data_opt{}},
        }};

    chain::header const header{4, null_hash, null_hash, 1293623863, 0, 0};
    chain::block const block{header, chain::transaction::list{coinbase, spend}};

    auto const filter = chain::build_regular_filter(block);
    REQUIRE(filter);
    // Two outpoints, two scripts and the data carrier. The empty script and
    // the second payment to a script are not items of their own.
    CHECK(filter->size() == 5);

    auto const key = chain::compact_filter_key(block.hash());
    CHECK(filter->match(key, outpoint_bytes(funding, 0)));
    CHECK(filter->match(key, outpoint_bytes(funding, 7)));
    CHECK(filter->match(key, script_of(0x11).bytes()));
    CHECK(filter->match(key, script_of(0x22).bytes()));
    CHECK(filter->match(key, carrier));

    // Not in it: the outpoint the coinbase input carries, an outpoint of the
    // same transaction that no input spends, and a script nothing pays to.
    CHECK( ! filter->match(key, outpoint_bytes(null_hash, chain::point::null_index)));
    CHECK( ! filter->match(key, outpoint_bytes(funding, 1)));
    CHECK( ! filter->match(key, script_of(0x33).bytes()));

    // The key is of the block: under the key of another block the items are
    // other numbers.
    hash_digest other{};
    other.fill(0x42);
    CHECK( ! filter->match(chain::compact_filter_key(other), script_of(0x22).bytes()));
}

TEST_CASE("filter headers chain: each commits to the filter and to the header before it", "[compact filter]") {
    auto const& vectors = library_vectors();
    auto const first = chain::build_regular_filter(block_of(vectors[1]));
    auto const second = chain::build_regular_filter(block_of(vectors[2]));
    REQUIRE(first);
    REQUIRE(second);

    auto const first_header = chain::compact_filter_header(*first, null_hash);
    auto const second_header = chain::compact_filter_header(*second, first_header);

    // The definition, spelled out.
    auto const expected = bitcoin_hash(splice(bitcoin_hash(second->to_data()), first_header));
    CHECK(second_header == expected);

    // Another filter under the same parent, and the same filter under another
    // parent, both give another header.
    CHECK(chain::compact_filter_header(*first, first_header) != second_header);
    CHECK(chain::compact_filter_header(*second, null_hash) != second_header);
}
