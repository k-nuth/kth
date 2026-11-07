// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// serialize_tests.cpp compactsize of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b): CompactSize values
// around every power of two up to MAX_SIZE (0x02000000) are written and read back unchanged. The read of a value above
// MAX_SIZE ("ReadCompactSize(): size too large") and the whole of `noncanonical` (a value written with more bytes than
// it needs is refused) are failed regressions kept as evidence outside this tree: Knuth's byte_reader accepts them.
// Original: Copyright (c) 2012-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.

#include <test_helpers.hpp>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/transaction.hpp>

using namespace kth;

TEST_CASE("BCHN serialize_tests - compactsize round trip up to MAX_SIZE", "[bchn_non_vm][serialize]") {
    constexpr uint64_t max_size = 0x02000000;
    std::vector<uint64_t> values;
    for (uint64_t i = 1; i <= max_size; i *= 2) {
        values.push_back(i - 1);
        values.push_back(i);
    }
    values.push_back(max_size);
    for (auto const v : values) {
        data_chunk raw(9);
        byte_writer writer(raw);
        REQUIRE(writer.write_variable_little_endian(v));
        auto const length = raw.size() - writer.remaining_size();
        // the writer is canonical: 1 byte below 253, 3 up to 0xffff, 5 up to 0xffffffff
        CHECK(length == (v < 253 ? 1u : v <= 0xffff ? 3u : v <= 0xffffffff ? 5u : 9u));
        byte_reader reader(raw);
        auto const read = reader.read_variable_little_endian();
        REQUIRE(read);
        CHECK(*read == v);
    }
    // 0xfd written with three bytes is the canonical form and is read back
    data_chunk const canonical{0xfd, 0xfd, 0x00};
    byte_reader reader(canonical);
    auto const read = reader.read_variable_little_endian();
    REQUIRE(read);
    CHECK(*read == 0xfd);
}

// The endpoints that read a CompactSize as a size: the number of inputs of a transaction, the number of transactions of a block and the
// length of a script. BCHN refuses a value above MAX_SIZE at all of them ("ReadCompactSize(): size too large"); Knuth's reader has no
// such bound (see the evidence), its endpoints bound the value by the 2 GB block cap and the data that follows. With data too short for
// the announced size, every endpoint refuses the same inputs as BCHN, with another reason.
TEST_CASE("BCHN serialize_tests - compactsize above MAX_SIZE at the endpoints that read a size", "[bchn_non_vm][serialize]") {
    namespace chain = kth::domain::chain;
    for (uint32_t count : {0x02000001u, 0x10000000u, 0x7fffffffu}) {
        auto const le = [&](data_chunk head) {
            head.insert(head.end(), {0xfe, uint8_t(count), uint8_t(count >> 8), uint8_t(count >> 16), uint8_t(count >> 24)});
            return head;
        };
        {   // transaction: version, then the input count
            auto raw = le({1, 0, 0, 0});
            byte_reader reader(raw);
            CHECK( ! chain::transaction::from_data(reader, true));
        }
        {   // block: an 80-byte header, then the transaction count
            auto raw = le(data_chunk(80, 0));
            byte_reader reader(raw);
            CHECK( ! chain::block::from_data(reader));
        }
        {   // script: its length
            data_chunk raw{uint8_t(0xfe), uint8_t(count), uint8_t(count >> 8), uint8_t(count >> 16), uint8_t(count >> 24)};
            byte_reader reader(raw);
            CHECK( ! chain::script::from_data(reader, true));
        }
    }
}
