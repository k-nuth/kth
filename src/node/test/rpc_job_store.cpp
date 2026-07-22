// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <memory>
#include <vector>

#include <kth/domain.hpp>
#include <kth/node/rpc/job_store.hpp>

using namespace kth;
using namespace kth::node::rpc;

namespace {

// Distinct transactions (by locktime) yield distinct txids -> distinct job ids.
transaction_const_ptr make_tx(uint32_t locktime) {
    return std::make_shared<domain::message::transaction>(
        domain::chain::transaction{1u, locktime, {}, {}});
}

std::vector<transaction_const_ptr> txs(std::initializer_list<uint32_t> locktimes) {
    std::vector<transaction_const_ptr> out;
    for (auto lt : locktimes) {
        out.push_back(make_tx(lt));
    }
    return out;
}

} // namespace

// Start Test Suite: rpc job_store tests

TEST_CASE("job_store add then get roundtrips", "[rpc job_store]") {
    job_store store(10, 3600);
    auto const id = store.add(txs({1, 2}));
    auto const got = store.get(id);
    REQUIRE(got.has_value());
    REQUIRE(got->size() == 2u);
}

TEST_CASE("job_store add is idempotent for equal selections", "[rpc job_store]") {
    job_store store(10, 3600);
    auto const a = store.add(txs({7, 8, 9}));
    auto const b = store.add(txs({7, 8, 9}));
    REQUIRE(a == b);
}

TEST_CASE("job_store distinct selections get distinct ids", "[rpc job_store]") {
    job_store store(10, 3600);
    REQUIRE(store.add(txs({1})) != store.add(txs({2})));
}

TEST_CASE("job_store returns nullopt for unknown id", "[rpc job_store]") {
    job_store store(10, 3600);
    REQUIRE_FALSE(store.get("deadbeef").has_value());
}

TEST_CASE("job_store evicts the oldest past the count bound", "[rpc job_store]") {
    job_store store(/*max_jobs*/ 2, /*ttl*/ 3600);
    auto const first = store.add(txs({1}));
    store.add(txs({2}));
    store.add(txs({3}));       // pushes the count to 3 -> evicts `first`
    REQUIRE_FALSE(store.get(first).has_value());
}
