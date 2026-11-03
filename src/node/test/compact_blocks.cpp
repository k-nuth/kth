// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <unordered_map>

#include <kth/domain/message/block.hpp>
#include <kth/domain/message/prefilled_transaction.hpp>
#include <kth/node/compact_blocks.hpp>

using namespace kth;
using namespace kth::node;

namespace {

domain::message::block reconstruction_block() {
    domain::chain::transaction::list transactions{
        {1, 0, {}, {}}, {2, 0, {}, {}}, {3, 0, {}, {}}, {4, 0, {}, {}}};
    domain::chain::block const body({}, transactions);
    return {{1, null_hash, body.generate_merkle_root(), 1, 1, 1}, std::move(transactions)};
}

domain::message::compact_block reconstruction_message(domain::message::block const& block) {
    auto const key = domain::message::hash(block, 42);
    return {block.header(), 42, {
        domain::message::compact_short_id(key, block.transactions()[1].hash()),
        domain::message::compact_short_id(key, block.transactions()[3].hash())}, {
        domain::message::prefilled_transaction::create(0, block.transactions()[0]).value(),
        domain::message::prefilled_transaction::create(2, block.transactions()[2]).value()}};
}

} // namespace

TEST_CASE("compact reconstruction fills the mempool transactions around prefilled indexes", "[compact_blocks]") {
    auto const block = reconstruction_block();
    auto const message = reconstruction_message(block);
    auto const rebuilt = reconstruct_compact_block(message,
        domain::chain::transaction::list{block.transactions()[3], {5, 0, {}, {}}, block.transactions()[1]});
    REQUIRE(rebuilt.status == compact_reconstruct::complete);
    CHECK(rebuilt.transactions == block.transactions());
    CHECK(rebuilt.missing.empty());
}

TEST_CASE("compact reconstruction requests absolute holes and checks the ordered response", "[compact_blocks]") {
    auto const block = reconstruction_block();
    auto const message = reconstruction_message(block);
    auto const partial = reconstruct_compact_block(message, domain::chain::transaction::list{});
    REQUIRE(partial.status == compact_reconstruct::missing);
    REQUIRE((partial.missing == std::vector<uint64_t>{1, 3}));
    CHECK(partial.transactions[1].is_null());
    CHECK(partial.transactions[3].is_null());

    auto const complete = accept_block_transactions(message, partial,
        {block.transactions()[1], block.transactions()[3]});
    REQUIRE(complete.status == compact_reconstruct::complete);
    CHECK(complete.transactions == block.transactions());
    CHECK(accept_block_transactions(message, partial, {}).status == compact_reconstruct::rejected);
    CHECK(accept_block_transactions(message, partial,
        {block.transactions()[3], block.transactions()[1]}).status == compact_reconstruct::rejected);
    CHECK(accept_block_transactions(message, complete,
        {block.transactions()[1], block.transactions()[3]}).status == compact_reconstruct::rejected);

    auto invalid = partial;
    invalid.transactions.pop_back();
    CHECK(accept_block_transactions(message, invalid,
        {block.transactions()[1], block.transactions()[3]}).status == compact_reconstruct::rejected);
    invalid = partial;
    invalid.missing = {3, 1};
    CHECK(accept_block_transactions(message, invalid,
        {block.transactions()[3], block.transactions()[1]}).status == compact_reconstruct::rejected);

    auto const duplicate_prefilled = domain::message::compact_block(message.header(), message.nonce(),
        message.short_ids(), {message.transactions()[0], message.transactions()[0]});
    CHECK(accept_block_transactions(duplicate_prefilled, partial,
        {block.transactions()[1], block.transactions()[3]}).status == compact_reconstruct::rejected);
    compact_reconstruction const empty{compact_reconstruct::missing, {}, {}};
    CHECK(accept_block_transactions(domain::message::compact_block{}, empty, {}).status
        == compact_reconstruct::rejected);
}

TEST_CASE("compact reconstruction rejects ambiguous IDs and invalid prefilled positions", "[compact_blocks]") {
    auto const block = reconstruction_block();
    auto const message = reconstruction_message(block);
    auto const duplicate = domain::message::compact_block(message.header(), message.nonce(),
        {message.short_ids()[0], message.short_ids()[0]}, message.transactions());
    CHECK(reconstruct_compact_block(duplicate, domain::chain::transaction::list{}).status
        == compact_reconstruct::collision);
    CHECK(reconstruct_compact_block(message,
        domain::chain::transaction::list{block.transactions()[1], block.transactions()[1]}).status
        == compact_reconstruct::collision);

    auto const at_zero = message.transactions()[0];
    auto const past_end = domain::message::prefilled_transaction::create(4, block.transactions()[2]).value();
    auto const null_tx = domain::message::prefilled_transaction::create(2, domain::chain::transaction{}).value();
    for (auto const& prefilled : {domain::message::prefilled_transaction::list{at_zero, at_zero},
            domain::message::prefilled_transaction::list{message.transactions()[1], at_zero},
            domain::message::prefilled_transaction::list{at_zero, past_end},
            domain::message::prefilled_transaction::list{at_zero, null_tx}}) {
        domain::message::compact_block const invalid(message.header(), message.nonce(), message.short_ids(), prefilled);
        CHECK(reconstruct_compact_block(invalid, domain::chain::transaction::list{}).status
            == compact_reconstruct::rejected);
    }
    CHECK(reconstruct_compact_block(domain::message::compact_block{}, domain::chain::transaction::list{}).status
        == compact_reconstruct::rejected);
}

TEST_CASE("compact reconstruction checks the merkle root before and after blocktxn", "[compact_blocks]") {
    auto const block = reconstruction_block();
    auto const message = reconstruction_message(block);
    domain::chain::header const wrong_header{1, null_hash, null_hash, 1, 1, 1};
    // The IDs use the altered header's keys, so only the merkle commitment is
    // wrong. A short-ID match alone must not let either path submit the block.
    domain::message::block const wrong_block(wrong_header, block.transactions());
    auto const wrong_message = reconstruction_message(wrong_block);
    CHECK(reconstruct_compact_block(wrong_message, block.transactions()).status == compact_reconstruct::rejected);
    auto const partial = reconstruct_compact_block(wrong_message, domain::chain::transaction::list{});
    REQUIRE(partial.status == compact_reconstruct::missing);
    CHECK(accept_block_transactions(wrong_message, partial,
        {block.transactions()[1], block.transactions()[3]}).status == compact_reconstruct::rejected);

    auto const one_missing = reconstruct_compact_block(message,
        domain::chain::transaction::list{block.transactions()[1]});
    REQUIRE(one_missing.missing == std::vector<uint64_t>{3});
    CHECK(accept_block_transactions(message, one_missing, {block.transactions()[3]}).status
        == compact_reconstruct::complete);
}

TEST_CASE("compact reconstruction bounds work for crowded short ID buckets", "[compact_blocks]") {
    std::unordered_map<uint64_t, size_t> probe;
    probe.reserve(14);
    domain::message::compact_block::short_id_list ids;
    for (uint64_t id = 0; id < 10000 && ids.size() < 14; ++id) {
        if (probe.bucket(id) == probe.bucket(0)) {
            ids.push_back(id);
        }
    }
    REQUIRE(ids.size() == 14);
    auto const block = reconstruction_block();
    domain::message::compact_block const crowded(block.header(), 42, std::move(ids), {
        domain::message::prefilled_transaction::create(0, block.transactions()[0]).value()});
    CHECK(reconstruct_compact_block(crowded, domain::chain::transaction::list{}).status
        == compact_reconstruct::collision);
}
