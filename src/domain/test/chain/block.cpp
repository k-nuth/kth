// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Test helper.
static
bool all_valid(chain::transaction::list const& transactions) {
    auto valid = true;

    for (auto const& tx : transactions) {
        valid = valid && tx.is_valid();

        for (auto const& input : tx.inputs()) {
            valid = valid && input.is_valid();
            valid = valid && input.script().is_valid();
        }

        for (auto const& output : tx.outputs()) {
            valid = valid && output.is_valid();
            valid = valid && output.script().is_valid();
        }
    }

    return valid;
}

// Start Test Suite: chain block tests

TEST_CASE("block proof2 genesis mainnet expected", "[chain block]") {
    auto const block = chain::block::genesis_mainnet();
    REQUIRE(block.proof() == 0x0000000100010001);
}

TEST_CASE("block locator size zero backoff returns top plus one", "[chain block]") {
    size_t top = 7u;
    REQUIRE(top + 1 == chain::block::locator_size(top));
}

TEST_CASE("block locator size positive backoff returns log plus eleven", "[chain block]") {
    size_t top = 138u;
    REQUIRE(18u == chain::block::locator_size(top));
}

TEST_CASE("block locator heights zero backoff returns top to zero", "[chain block]") {
    const chain::block::indexes expected{7u, 6u, 5u, 4u, 3u, 2u, 1u, 0u};
    size_t top = 7u;
    auto result = chain::block::locator_heights(top);
    REQUIRE(expected.size() == result.size());
    REQUIRE(expected == result);
}

TEST_CASE("block locator heights positive backoff returns top plus log offset to zero", "[chain block]") {
    const chain::block::indexes expected{
        138u, 137u, 136u, 135u, 134u, 133u, 132u, 131u, 130u,
        129u, 128u, 126u, 122u, 114u, 98u, 66u, 2u, 0u};

    size_t top = 138u;
    auto result = chain::block::locator_heights(top);
    REQUIRE(expected.size() == result.size());
    REQUIRE(expected == result);
}

TEST_CASE("block constructor 1 always invalid", "[chain block]") {
    chain::block instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("block constructor 2 always equals params", "[chain block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::block instance(header, transactions);
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 3 always equals params", "[chain block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    // These must be non-const.
    chain::header dup_header(header);
    chain::transaction::list dup_transactions(transactions);

    chain::block instance(std::move(dup_header), std::move(dup_transactions));
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 4 always equals params", "[chain block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::block value(header, transactions);
    chain::block instance(value);
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block constructor 5 always equals params", "[chain block]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    // This must be non-const.
    chain::block value(header, transactions);

    chain::block instance(std::move(value));
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block  hash  always  returns header hash", "[chain block]") {
    chain::block instance;
    REQUIRE(instance.header().hash() == instance.hash());
}

TEST_CASE("block  is valid merkle root  uninitialized  returns true", "[chain block]") {
    chain::block instance;
    REQUIRE(instance.is_valid_merkle_root());
}

TEST_CASE("block  is valid merkle root  non empty tx invalid block  returns false", "[chain block]") {
    chain::block instance;
    instance.set_transactions(chain::transaction::list{chain::transaction{}});
    REQUIRE( ! instance.is_valid_merkle_root());
}

TEST_CASE("block  is valid merkle root  valid  returns true", "[chain block]") {
    auto const raw_block = to_chunk(base16_literal(
        "010000007f110631052deeee06f0754a3629ad7663e56359fd5f3aa7b3e30a0000000"
        "0005f55996827d9712147a8eb6d7bae44175fe0bcfa967e424a25bfe9f4dc118244d6"
        "7fb74c9d8e2f1bea5ee82a03010000000100000000000000000000000000000000000"
        "00000000000000000000000000000ffffffff07049d8e2f1b0114ffffffff0100f205"
        "2a0100000043410437b36a7221bc977dce712728a954e3b5d88643ed5aef46660ddcf"
        "eeec132724cd950c1fdd008ad4a2dfd354d6af0ff155fc17c1ee9ef802062feb07ef1"
        "d065f0ac000000000100000001260fd102fab456d6b169f6af4595965c03c2296ecf2"
        "5bfd8790e7aa29b404eff010000008c493046022100c56ad717e07229eb93ecef2a32"
        "a42ad041832ffe66bd2e1485dc6758073e40af022100e4ba0559a4cebbc7ccb5d14d1"
        "312634664bac46f36ddd35761edaae20cefb16f01410417e418ba79380f462a60d8dd"
        "12dcef8ebfd7ab1741c5c907525a69a8743465f063c1d9182eea27746aeb9f1f52583"
        "040b1bc341b31ca0388139f2f323fd59f8effffffff0200ffb2081d0000001976a914"
        "fc7b44566256621affb1541cc9d59f08336d276b88ac80f0fa02000000001976a9146"
        "17f0609c9fabb545105f7898f36b84ec583350d88ac00000000010000000122cd6da2"
        "6eef232381b1a670aa08f4513e9f91a9fd129d912081a3dd138cb013010000008c493"
        "0460221009339c11b83f234b6c03ebbc4729c2633cbc8cbd0d15774594bfedc45c4f9"
        "9e2f022100ae0135094a7d651801539df110a028d65459d24bc752d7512bc8a9f78b4"
        "ab368014104a2e06c38dc72c4414564f190478e3b0d01260f09b8520b196c2f6ec3d0"
        "6239861e49507f09b7568189efe8d327c3384a4e488f8c534484835f8020b3669e5ae"
        "bffffffff0200ac23fc060000001976a914b9a2c9700ff9519516b21af338d28d53dd"
        "f5349388ac00743ba40b0000001976a914eb675c349c474bec8dea2d79d12cff6f330"
        "ab48788ac00000000"));

    byte_reader reader(raw_block);
    auto const result = chain::block::from_data(reader);
    REQUIRE(result);
    REQUIRE(result->is_valid_merkle_root());
}

// Start Test Suite: block serialization tests

TEST_CASE("block from data insufficient bytes  failure", "[block serialization]") {
    data_chunk data(10);
    byte_reader reader(data);
    auto const result = chain::block::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("block from data insufficient transaction bytes  failure", "[block serialization]") {
    data_chunk const data = to_chunk(base16_literal(
        "010000007f110631052deeee06f0754a3629ad7663e56359fd5f3aa7b3e30a00"
        "000000005f55996827d9712147a8eb6d7bae44175fe0bcfa967e424a25bfe9f4"
        "dc118244d67fb74c9d8e2f1bea5ee82a03010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff07049d8e2f1b"
        "0114ffffffff0100f2052a0100000043410437b36a7221bc977dce712728a954"));

    byte_reader reader(data);
    auto const result = chain::block::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("block genesis mainnet valid structure", "[block serialization]") {
    auto const genesis = chain::block::genesis_mainnet();
    REQUIRE(genesis.is_valid());
    REQUIRE(genesis.transactions().size() == 1u);
    REQUIRE(genesis.header().merkle() == genesis.generate_merkle_root());
}

TEST_CASE("block genesis testnet valid structure", "[block serialization]") {
    auto const genesis = chain::block::genesis_testnet();
    REQUIRE(genesis.is_valid());
    REQUIRE(genesis.transactions().size() == 1u);
    REQUIRE(genesis.header().merkle() == genesis.generate_merkle_root());
}

TEST_CASE("block genesis regtest valid structure", "[block serialization]") {
    auto const genesis = chain::block::genesis_regtest();
    REQUIRE(genesis.is_valid());
    REQUIRE(genesis.transactions().size() == 1u);
    REQUIRE(genesis.header().merkle() == genesis.generate_merkle_root());
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("block genesis testnet4 valid structure", "[block serialization]") {
    auto const genesis = chain::block::genesis_testnet4();
    REQUIRE(genesis.is_valid());
    REQUIRE(genesis.transactions().size() == 1u);
    REQUIRE(genesis.header().merkle() == genesis.generate_merkle_root());
}

TEST_CASE("block genesis scalenet valid structure", "[block serialization]") {
    auto const genesis = chain::block::genesis_scalenet();
    REQUIRE(genesis.is_valid());
    REQUIRE(genesis.transactions().size() == 1u);
    REQUIRE(genesis.header().merkle() == genesis.generate_merkle_root());
}

TEST_CASE("block genesis chipnet valid structure", "[block serialization]") {
    auto const genesis = chain::block::genesis_chipnet();
    REQUIRE(genesis.is_valid());
    REQUIRE(genesis.transactions().size() == 1u);
    REQUIRE(genesis.header().merkle() == genesis.generate_merkle_root());
}

#endif

TEST_CASE("block from data genesis mainnet  success", "[block serialization]") {
    auto const genesis = chain::block::genesis_mainnet();
    REQUIRE(genesis.serialized_size() == 285u);
    REQUIRE(genesis.header().serialized_size() == 80u);

    // Save genesis block.
    auto raw_block = genesis.to_data();
    REQUIRE(raw_block.size() == 285u);

    // Reload genesis block.
    byte_reader reader(raw_block);
    auto const result = chain::block::from_data(reader);
    REQUIRE(result);
    auto const& block = *result;

    REQUIRE(block.is_valid());
    REQUIRE(genesis.header() == block.header());

    // Verify merkle root from transactions.
    REQUIRE(genesis.header().merkle() == block.generate_merkle_root());
}

TEST_CASE("block  factory from data 2  genesis mainnet  success", "[block serialization]") {
    auto const genesis = chain::block::genesis_mainnet();
    REQUIRE(genesis.serialized_size() == 285u);
    REQUIRE(genesis.header().serialized_size() == 80u);

    // Save genesis block.
    auto raw_block = genesis.to_data();
    REQUIRE(raw_block.size() == 285u);

    // Reload genesis block.
    byte_reader reader(raw_block);
    auto const result = chain::block::from_data(reader);
    REQUIRE(result);
    auto const& block = *result;

    REQUIRE(block.is_valid());
    REQUIRE(genesis.header() == block.header());

    // Verify merkle root from transactions.
    REQUIRE(genesis.header().merkle() == block.generate_merkle_root());
}

TEST_CASE("block  factory from data 3  genesis mainnet  success", "[block serialization]") {
    auto const genesis = chain::block::genesis_mainnet();
    REQUIRE(genesis.serialized_size() == 285u);
    REQUIRE(genesis.header().serialized_size() == 80u);

    // Save genesis block.
    data_chunk raw_block = genesis.to_data();
    REQUIRE(raw_block.size() == 285u);

    // Reload genesis block.
    byte_reader reader(raw_block);
    auto const result = chain::block::from_data(reader);
    REQUIRE(result);
    auto const& block = *result;

    REQUIRE(block.is_valid());
    REQUIRE(genesis.header() == block.header());

    // Verify merkle root from transactions.
    REQUIRE(genesis.header().merkle() == block.generate_merkle_root());
}

// End Test Suite

// Start Test Suite: block generate merkle root tests

TEST_CASE("block  generate merkle root  block with zero transactions  matches null hash", "[block generate merkle root]") {
    chain::block empty;
    REQUIRE(empty.generate_merkle_root() == null_hash);
}

TEST_CASE("block  generate merkle root  block with multiple transactions  matches historic data", "[block generate merkle root]") {
    // encodes the 100,000 block data.
    data_chunk const raw = to_chunk(base16_literal(
        "010000007f110631052deeee06f0754a3629ad7663e56359fd5f3aa7b3e30a00"
        "000000005f55996827d9712147a8eb6d7bae44175fe0bcfa967e424a25bfe9f4"
        "dc118244d67fb74c9d8e2f1bea5ee82a03010000000100000000000000000000"
        "00000000000000000000000000000000000000000000ffffffff07049d8e2f1b"
        "0114ffffffff0100f2052a0100000043410437b36a7221bc977dce712728a954"
        "e3b5d88643ed5aef46660ddcfeeec132724cd950c1fdd008ad4a2dfd354d6af0"
        "ff155fc17c1ee9ef802062feb07ef1d065f0ac000000000100000001260fd102"
        "fab456d6b169f6af4595965c03c2296ecf25bfd8790e7aa29b404eff01000000"
        "8c493046022100c56ad717e07229eb93ecef2a32a42ad041832ffe66bd2e1485"
        "dc6758073e40af022100e4ba0559a4cebbc7ccb5d14d1312634664bac46f36dd"
        "d35761edaae20cefb16f01410417e418ba79380f462a60d8dd12dcef8ebfd7ab"
        "1741c5c907525a69a8743465f063c1d9182eea27746aeb9f1f52583040b1bc34"
        "1b31ca0388139f2f323fd59f8effffffff0200ffb2081d0000001976a914fc7b"
        "44566256621affb1541cc9d59f08336d276b88ac80f0fa02000000001976a914"
        "617f0609c9fabb545105f7898f36b84ec583350d88ac00000000010000000122"
        "cd6da26eef232381b1a670aa08f4513e9f91a9fd129d912081a3dd138cb01301"
        "0000008c4930460221009339c11b83f234b6c03ebbc4729c2633cbc8cbd0d157"
        "74594bfedc45c4f99e2f022100ae0135094a7d651801539df110a028d65459d2"
        "4bc752d7512bc8a9f78b4ab368014104a2e06c38dc72c4414564f190478e3b0d"
        "01260f09b8520b196c2f6ec3d06239861e49507f09b7568189efe8d327c3384a"
        "4e488f8c534484835f8020b3669e5aebffffffff0200ac23fc060000001976a9"
        "14b9a2c9700ff9519516b21af338d28d53ddf5349388ac00743ba40b00000019"
        "76a914eb675c349c474bec8dea2d79d12cff6f330ab48788ac00000000"));

    byte_reader reader(raw);
    auto const result = chain::block::from_data(reader);
    REQUIRE(result);
    auto const& block100k = *result;
    REQUIRE(block100k.is_valid());

    auto const serial = block100k.to_data();
    REQUIRE(raw == serial);

    auto const header = block100k.header();
    auto const transactions = block100k.transactions();
    REQUIRE(all_valid(transactions));
    REQUIRE(header.merkle() == block100k.generate_merkle_root());
}

TEST_CASE("block  header accessor  always  returns initialized value", "[block generate merkle root]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::block instance(header, transactions);
    REQUIRE(header == instance.header());
}

TEST_CASE("block  header setter 1  roundtrip  success", "[block generate merkle root]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::block instance;
    REQUIRE(header != instance.header());
    instance.set_header(header);
    REQUIRE(header == instance.header());
}

TEST_CASE("block  header setter 2  roundtrip  success", "[block generate merkle root]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    // This must be non-const.
    chain::header dup_header(header);

    chain::block instance;
    REQUIRE(header != instance.header());
    instance.set_header(std::move(dup_header));
    REQUIRE(header == instance.header());
}

TEST_CASE("block  transactions accessor  always  returns initialized value", "[block generate merkle root]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::block instance(header, transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block  transactions setter 1  roundtrip  success", "[block generate merkle root]") {
    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    chain::block instance;
    REQUIRE(transactions != instance.transactions());
    instance.set_transactions(transactions);
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block  transactions setter 2  roundtrip  success", "[block generate merkle root]") {
    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    // This must be non-const.
    chain::transaction::list dup_transactions(transactions);

    chain::block instance;
    REQUIRE(transactions != instance.transactions());
    instance.set_transactions(std::move(dup_transactions));
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block  operator assign equals  always  matches equivalent", "[block generate merkle root]") {
    chain::header const header(10u,
                               hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                               hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                               531234u,
                               6523454u,
                               68644u);

    chain::transaction::list const transactions{
        chain::transaction(1, 48, {}, {}),
        chain::transaction(2, 32, {}, {}),
        chain::transaction(4, 16, {}, {})};

    // This must be non-const.
    chain::block value(header, transactions);

    REQUIRE(value.is_valid());
    chain::block instance;
    REQUIRE( ! instance.is_valid());
    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(header == instance.header());
    REQUIRE(transactions == instance.transactions());
}

TEST_CASE("block  operator boolean equals  duplicates  returns true", "[block generate merkle root]") {
    const chain::block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    chain::block instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("block  operator boolean equals  differs  returns false", "[block generate merkle root]") {
    const chain::block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    chain::block instance;
    REQUIRE( ! (instance == expected));
}

TEST_CASE("block  operator boolean not equals  duplicates  returns false", "[block generate merkle root]") {
    const chain::block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    chain::block instance(expected);
    REQUIRE( ! (instance != expected));
}

TEST_CASE("block  operator boolean not equals  differs  returns true", "[block generate merkle root]") {
    const chain::block expected(
        chain::header(10u,
                      hash_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"),
                      hash_literal("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b"),
                      531234u,
                      6523454u,
                      68644u),
        {chain::transaction(1, 48, {}, {}),
         chain::transaction(2, 32, {}, {}),
         chain::transaction(4, 16, {}, {})});

    chain::block instance;
    REQUIRE(instance != expected);
}

// End Test Suite

// Start Test Suite: block is distinct transaction set tests

TEST_CASE("block  distinct transactions  empty  true", "[block is distinct transaction set]") {
    chain::block value;
    REQUIRE(value.is_distinct_transaction_set());
}

TEST_CASE("validate block  is distinct tx set  single  true", "[block is distinct transaction set]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}});
    REQUIRE(value.is_distinct_transaction_set());
}

TEST_CASE("validate block  is distinct tx set  duplicate  false", "[block is distinct transaction set]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}, {1, 0, {}, {}}});
    REQUIRE( ! value.is_distinct_transaction_set());
}

TEST_CASE("validate block  is distinct tx set  distinct by version  true", "[block is distinct transaction set]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}, {2, 0, {}, {}}, {3, 0, {}, {}}});
    REQUIRE(value.is_distinct_transaction_set());
}

TEST_CASE("validate block  is distinct tx set  partialy distinct by version  false", "[block is distinct transaction set]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}, {2, 0, {}, {}}, {2, 0, {}, {}}});
    REQUIRE( ! value.is_distinct_transaction_set());
}

TEST_CASE("validate block  is distinct tx set  partialy distinct not adjacent by version  false", "[block is distinct transaction set]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}, {2, 0, {}, {}}, {1, 0, {}, {}}});
    REQUIRE( ! value.is_distinct_transaction_set());
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("validate block is cash pow valid true", "[block is distinct transaction set]") {
    uint32_t old_bits = 402736949;
    const domain::chain::compact bits(old_bits);
    uint256_t target(bits);
    REQUIRE(domain::chain::compact(domain::chain::chain_state::difficulty_adjustment_cash(target)).normal() == 402757890);
}
#endif  //KTH_CURRENCY_BCH

// End Test Suite

// Start Test Suite: block is forward reference tests

TEST_CASE("block  is forward reference  no transactions  false", "[block is forward reference]") {
    chain::block value;
    REQUIRE( ! value.is_forward_reference());
}

TEST_CASE("block  is forward reference  multiple empty transactions  false", "[block is forward reference]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}, {2, 0, {}, {}}});
    REQUIRE( ! value.is_forward_reference());
}

TEST_CASE("block  is forward reference  backward reference  false", "[block is forward reference]") {
    chain::block value;
    chain::transaction before{2, 0, {}, {}};
    chain::transaction after{1, 0, {{{before.hash(), 0}, {}, 0}}, {}};
    value.set_transactions({before, after});
    REQUIRE( ! value.is_forward_reference());
}

TEST_CASE("block  is forward reference  duplicate transactions  false", "[block is forward reference]") {
    chain::block value;
    value.set_transactions({{1, 0, {}, {}}, {1, 0, {}, {}}});
    REQUIRE( ! value.is_forward_reference());
}

TEST_CASE("block  is forward reference  coinbase and multiple empty transactions  false", "[block is forward reference]") {
    chain::block value;
    chain::transaction coinbase{1, 0, {{{null_hash, chain::point::null_index}, {}, 0}}, {}};
    value.set_transactions({coinbase, {2, 0, {}, {}}, {3, 0, {}, {}}});
    REQUIRE( ! value.is_forward_reference());
}

TEST_CASE("block  is forward reference  forward reference  true", "[block is forward reference]") {
    chain::block value;
    chain::transaction after{2, 0, {}, {}};
    chain::transaction before{1, 0, {{{after.hash(), 0}, {}, 0}}, {}};
    value.set_transactions({before, after});
    REQUIRE(value.is_forward_reference());
}

// End Test Suite

// End Test Suite
