// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Header-level scenarios of Bitcoin Cash Node's test/functional/feature_block.py (commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b): b47 "Reject a block with invalid work",
// b48 "Reject a block with a timestamp >2 hours in the future", b50 "Reject a block with
// incorrect POW limit", b54 "Reject a block with timestamp before MedianTimePast", driven
// through the real header_organizer::add_headers on a regtest chain.
// Original: Copyright (c) The Bitcoin Core / Bitcoin developers, MIT license.

#include <test_helpers.hpp>

#include <algorithm>
#include <vector>

#include "bchn_block_builder.hpp"

using namespace kth;
using namespace kth::test;
using kth::domain::chain::header;

namespace {

constexpr uint32_t chain_length = 14;

// Extends a fresh chain of coinbase-only blocks and returns what add_headers says about the
// header built by `make(prev_hash, last_timestamp)` on top of it.
struct outcome { size_t added; code error; };

template <typename MakeHeader>
outcome add_after_chain(char const* tag, MakeHeader make) {
    chain_fixture fixture(tag);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    regtest_chain chain(chain_length);
    chain.extend_to(chain_length);
    domain::message::header::list headers;
    for (auto const& blk : chain.blocks()) headers.push_back(blk->header());
    auto const base = fixture.organizer().add_headers(headers);
    REQUIRE(base.headers_added == headers.size());
    auto const& tip = chain.at(chain_length);
    domain::message::header::list next;
    next.push_back(make(tip, chain));
    auto const result = fixture.organizer().add_headers(next);
    return {result.headers_added, result.error};
}

// Regtest header on `prev` with the given time and bits, proof of work solved against `bits`.
domain::message::header solved_header(hash_digest const& prev, uint32_t timestamp, uint32_t bits, bool want_valid_work) {
    for (uint64_t nonce = 0; nonce <= 0xffffffffull; ++nonce) {
        header candidate{0x20000000, prev, null_hash, timestamp, bits, uint32_t(nonce)};
        if (candidate.is_valid_proof_of_work(domain::chain::hash(candidate), false) == want_valid_work) {
            return domain::message::header(std::move(candidate));
        }
    }
    throw std::runtime_error("no nonce");
}

} // namespace

namespace {

uint32_t median_time_past_of(regtest_chain const& chain) {
    std::vector<uint32_t> times;
    for (uint32_t h = chain.height() - 10; h <= chain.height(); ++h) times.push_back(chain.at(h).header().timestamp());
    std::sort(times.begin(), times.end());
    return times[5];
}

} // namespace

TEST_CASE("BCHN feature_block header control - the next header on the chain is accepted", "[bchn_non_vm][header]") {
    auto const r = add_after_chain("bchn_hdr_ok", [](auto const& tip, auto const&) {
        return solved_header(tip.hash(), tip.header().timestamp() + 600, regtest_bits, true);
    });
    CHECK( ! r.error);
    CHECK(r.added == 1u);
}

// b47: invalid work
TEST_CASE("BCHN feature_block b47 - header whose hash does not meet its target is rejected", "[bchn_non_vm][header]") {
    auto const r = add_after_chain("bchn_hdr_pow", [](auto const& tip, auto const&) {
        return solved_header(tip.hash(), tip.header().timestamp() + 600, regtest_bits, false);
    });
    CHECK(r.error == error::invalid_proof_of_work);
    CHECK(r.added == 0u);
}

// b48: timestamp more than 2 hours in the future (one hour ahead is the control)
TEST_CASE("BCHN feature_block b48 - header more than 2 hours in the future is rejected", "[bchn_non_vm][header]") {
    auto const now = uint32_t(zulu_time());
    auto const ok = add_after_chain("bchn_hdr_future_ok", [&](auto const& tip, auto const&) {
        return solved_header(tip.hash(), now + 60 * 60, regtest_bits, true);
    });
    CHECK( ! ok.error);
    auto const bad = add_after_chain("bchn_hdr_future", [&](auto const& tip, auto const&) {
        return solved_header(tip.hash(), now + 3 * 60 * 60, regtest_bits, true);
    });
    CHECK(bad.error == error::futuristic_timestamp);
    CHECK(bad.added == 0u);
}

// b50: bits other than the required ones
TEST_CASE("BCHN feature_block b50 - header with the wrong compact target is rejected", "[bchn_non_vm][header]") {
    auto const r = add_after_chain("bchn_hdr_bits", [](auto const& tip, auto const&) {
        return solved_header(tip.hash(), tip.header().timestamp() + 600, regtest_bits - 1, true);
    });
    CHECK(r.error == error::incorrect_proof_of_work);
    CHECK(r.added == 0u);
}

// b54: timestamp not after the median time past (equal is rejected, one second later is the control)
TEST_CASE("BCHN feature_block b54 - header at or before the median time past is rejected", "[bchn_non_vm][header]") {
    auto const mtp_equal = add_after_chain("bchn_hdr_mtp_eq", [](auto const& tip, auto const& chain) {
        return solved_header(tip.hash(), median_time_past_of(chain), regtest_bits, true);
    });
    CHECK(mtp_equal.error == error::timestamp_too_early);
    CHECK(mtp_equal.added == 0u);
    auto const mtp_before = add_after_chain("bchn_hdr_mtp_before", [](auto const& tip, auto const& chain) {
        return solved_header(tip.hash(), median_time_past_of(chain) - 1, regtest_bits, true);
    });
    CHECK(mtp_before.error == error::timestamp_too_early);
    auto const mtp_after = add_after_chain("bchn_hdr_mtp_after", [](auto const& tip, auto const& chain) {
        return solved_header(tip.hash(), median_time_past_of(chain) + 1, regtest_bits, true);
    });
    CHECK( ! mtp_after.error);
}
