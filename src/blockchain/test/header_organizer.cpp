// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <atomic>
#include <thread>

#include <kth/blockchain.hpp>

using namespace kth;
using namespace kth::blockchain;

// Note: We use explicit kth::domain::chain:: qualification for header types
// to avoid ambiguity with kth::domain::message::header

// =============================================================================
// Helper functions
// =============================================================================

namespace {

// Create a deterministic hash from an integer
hash_digest make_hash(uint32_t id) {
    hash_digest hash{};
    hash[0] = uint8_t(id & 0xFF);
    hash[1] = uint8_t((id >> 8) & 0xFF);
    hash[2] = uint8_t((id >> 16) & 0xFF);
    hash[3] = uint8_t((id >> 24) & 0xFF);
    return hash;
}

// Create a header with specific prev_hash
domain::chain::header make_header_with_prev(hash_digest const& prev_hash, uint32_t height) {
    return domain::chain::header{1, prev_hash, make_hash(height + 1000), height * 600, 0x1d00ffff, height};
}

// Build a simple chain of headers in the index
// Returns the hash of the last (tip) header
hash_digest build_chain(header_index& index, size_t length) {
    hash_digest prev_hash = null_hash;

    for (size_t i = 0; i < length; ++i) {
        auto hdr = make_header_with_prev(prev_hash, uint32_t(i));
        auto hash = kth::domain::chain::hash(hdr);

        auto result = index.add(hash, hdr);
        REQUIRE(result.inserted);

        prev_hash = hash;
    }

    return prev_hash;
}

// Create test settings
settings make_test_settings() {
    settings s(domain::config::network::mainnet);
    // Disable checkpoints for testing
    s.checkpoints.clear();
    return s;
}

} // namespace

// =============================================================================
// Stale batch detection tests - Issue #11 fix (2026-02-02)
// =============================================================================

TEST_CASE("header_organizer stale batch returns stale_chain error", "[header_organizer][stale]") {
    // Setup: Create an index with a chain of 10 headers
    header_index index;
    auto tip_hash = build_chain(index, 10);
    REQUIRE(index.size() == 10);

    // Get hash at height 5 (an ancestor of the tip)
    auto ancestor_hash = index.get_hash(5);
    REQUIRE(ancestor_hash != null_hash);

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Create a batch of headers that connects to the ancestor (height 5), not the tip (height 9)
    // This simulates a stale/duplicate retry request
    domain::message::header::list stale_headers;
    hash_digest prev = ancestor_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(100 + i));  // Different heights to avoid collision
        prev = kth::domain::chain::hash(hdr);
        stale_headers.push_back(std::move(hdr));
    }

    // Add the stale batch
    auto result = organizer.add_headers(stale_headers);

    // 2026-02-02: Should return stale_chain error, NOT success
    // This prevents the sync coordinator from marking header sync as "complete"
    REQUIRE(result.error == error::stale_chain);
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer normal batch returns success", "[header_organizer][stale]") {
    // Setup: Create an index with a chain of 10 headers
    header_index index;
    auto tip_hash = build_chain(index, 10);
    REQUIRE(index.size() == 10);

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Create a batch of headers that connects to the tip (normal case)
    domain::message::header::list new_headers;
    hash_digest prev = tip_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(10 + i));
        prev = kth::domain::chain::hash(hdr);
        new_headers.push_back(std::move(hdr));
    }

    // Add the normal batch
    auto result = organizer.add_headers(new_headers);

    // Should succeed and add all headers
    REQUIRE(!result.error);
    REQUIRE(result.headers_added == 3);
    REQUIRE(index.size() == 13);
}

TEST_CASE("header_organizer empty batch returns success with zero count", "[header_organizer][stale]") {
    // Setup: Create an index with a chain of 10 headers
    header_index index;
    (void)build_chain(index, 10);  // tip_hash not used in this test

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Add empty batch
    domain::message::header::list empty_headers;
    auto result = organizer.add_headers(empty_headers);

    // Empty batch should succeed with zero count (this IS a valid "sync complete" signal)
    REQUIRE(!result.error);
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer note_block_validated respects the startup window", "[header_organizer][finalization]") {
    // A freshly constructed organizer is within the finalization startup window
    // (uptime < finalization_delay), so no block finalizes even once blocks are
    // validated well past the depth threshold. (The finalization rule itself is
    // covered by the finalization component tests.)
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    REQUIRE(organizer.finalized_height() == -1);
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == -1);
}

TEST_CASE("header_organizer duplicate headers in same batch", "[header_organizer][stale]") {
    // Setup
    header_index index;
    auto tip_hash = build_chain(index, 5);

    // Create organizer and sync its tip with the index
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();  // Must sync tip after populating index

    // Create headers that connect to tip
    domain::message::header::list headers;
    hash_digest prev = tip_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(5 + i));
        prev = kth::domain::chain::hash(hdr);
        headers.push_back(std::move(hdr));
    }

    // Add first time - should succeed
    auto result1 = organizer.add_headers(headers);
    REQUIRE(!result1.error);
    REQUIRE(result1.headers_added == 3);

    // Add same headers again - should be detected as stale
    auto result2 = organizer.add_headers(headers);
    REQUIRE(result2.error == error::stale_chain);
    REQUIRE(result2.headers_added == 0);
}

// =============================================================================
// Fork detection + finalization enforcement
// =============================================================================

namespace {

// A side branch off `fork_prev` (the fork ancestor's hash), with ids from
// `id_base` so hashes never collide with the main chain.
domain::message::header::list make_branch(hash_digest fork_prev, size_t length, uint32_t id_base) {
    domain::message::header::list branch;
    hash_digest prev = fork_prev;
    for (uint32_t i = 0; i < length; ++i) {
        auto hdr = make_header_with_prev(prev, id_base + i);
        prev = kth::domain::chain::hash(hdr);
        branch.push_back(std::move(hdr));
    }
    return branch;
}

// Test settings with finalization forced to fire immediately (no 2h delay), so
// note_block_validated can finalize deterministically without waiting.
settings make_finalizing_settings() {
    auto s = make_test_settings();
    s.finalization_delay_seconds = 0;   // headers eligible the instant they're deep enough
    // max_reorg_depth stays at its default (10).
    return s;
}

} // namespace

TEST_CASE("header_organizer flags a heavier side branch as a reorg candidate", "[header_organizer][fork]") {
    header_index index;
    (void)build_chain(index, 10);                 // tip at height 9
    auto const fork_prev = index.get_hash(5);

    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // 6 blocks off height 5 -> head at height 11, more work than the height-9 tip.
    auto branch = make_branch(fork_prev, 6, 200);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == 5);
    REQUIRE(result.error == error::stale_chain);      // tip not switched
    REQUIRE(result.headers_added == 0);
    REQUIRE(organizer.header_height() == 9);
    REQUIRE(index.size() == 16);                       // branch stored (10 + 6)
}

TEST_CASE("header_organizer rejects a new branch forking below the finalized block", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip at height 29, received_time 0
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    organizer.note_block_validated(29);           // finalize 29 - 10 = 19
    REQUIRE(organizer.finalized_height() == 19);

    auto const size_before = index.size();
    // New branch off height 15 (below the finalized height 19) -> rejected + penalize.
    auto branch = make_branch(index.get_hash(15), 8, 500);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.error == error::finalized_header_violation);
    REQUIRE(result.headers_added == 0);
    REQUIRE(index.size() == size_before);          // nothing stored
}

TEST_CASE("header_organizer allows a branch forking above the finalized block", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip at height 29
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == 19);

    // 8 blocks off height 22 (above finalized) -> head at height 30, out-works the tip.
    auto branch = make_branch(index.get_hash(22), 8, 600);
    auto result = organizer.add_headers(branch);

    REQUIRE(result.error != error::finalized_header_violation);
    REQUIRE(result.reorg_candidate);
    REQUIRE(result.reorg_fork_height == 22);
}

TEST_CASE("header_organizer does not penalize re-sent known headers below the finalized block", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == 19);

    // Re-send already-known main-chain headers at heights 15..17 (below finalized).
    // This is a stale duplicate, not a violation — must not be penalized.
    domain::message::header::list known;
    known.push_back(index.get_header(15));
    known.push_back(index.get_header(16));
    known.push_back(index.get_header(17));
    auto result = organizer.add_headers(known);

    REQUIRE(result.error == error::stale_chain);   // benign duplicate, not a violation
    REQUIRE(result.headers_added == 0);
}

TEST_CASE("header_organizer rejects a new header after a known one on a sub-finalized branch", "[header_organizer][finalization]") {
    header_index index;
    (void)build_chain(index, 30);                 // tip 29, finalized -> 19
    auto settings = make_finalizing_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();
    organizer.note_block_validated(29);
    REQUIRE(organizer.finalized_height() == 19);

    auto const size_before = index.size();
    // getheaders-overlap: a KNOWN main-chain header at height 15 (below the finalized
    // height 19) followed by a NEW header forking off it. The known front must not let
    // the new sub-finalized header bypass the finalization check.
    domain::message::header::list batch;
    batch.push_back(index.get_header(15));                             // known, below finalized
    batch.push_back(make_header_with_prev(index.get_hash(15), 900));   // new, forks below finalized

    auto result = organizer.add_headers(batch);

    REQUIRE(result.error == error::finalized_header_violation);
    REQUIRE(result.headers_added == 0);
    REQUIRE(index.size() == size_before);          // nothing stored
}

TEST_CASE("adopting a tip republishes the heights with it", "[header_organizer][fork]") {
    // The two halves of the tip — the index the organizer remembers, and the
    // height mapping everything else reads — must never name different branches.
    //
    // They could: the switch used to publish the mapping itself, so a header
    // batch that validated against the old tip could publish its own extension
    // in between, and adopt_tip would then move only the index. This reproduces
    // that end state directly (extend the old tip, then adopt the branch) rather
    // than racing for the interleaving that produces it.
    header_index index;
    (void)build_chain(index, 10);                  // tip at height 9
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // A side branch off height 5, stored but not active.
    auto branch = make_branch(index.get_hash(5), 6, 800);
    REQUIRE(organizer.add_headers(branch).reorg_candidate);
    auto const branch_head = index.find(kth::domain::chain::hash(branch.back()));
    REQUIRE(branch_head != header_index::null_index);

    // An extension of the old tip publishes, as it would while a switch is being
    // executed elsewhere.
    domain::message::header::list extension;
    extension.push_back(make_header_with_prev(index.get_hash(9), 999));
    REQUIRE(organizer.add_headers(extension).headers_added == 1);
    REQUIRE(index.active_tip_height() == 10);

    // Now the switch's tip lands. Both halves move.
    organizer.adopt_tip(branch_head);

    CHECK(organizer.tip_index() == branch_head);
    CHECK(index.active_tip_height() == index.get_height(branch_head));
    CHECK(index.active_at(index.get_height(branch_head)) == branch_head);

    // And every height above the fork names the branch, not the extension.
    for (int32_t h = 6; h <= index.get_height(branch_head); ++h) {
        auto const idx = index.active_at(h);
        REQUIRE(idx != header_index::null_index);
        CHECK(index.get_ancestor(branch_head, h) == idx);
    }
}

// =============================================================================
// Active chain (height -> index)
// =============================================================================

TEST_CASE("active chain maps heights to indices after a linear sync", "[header_organizer][active_chain]") {
    header_index index;
    (void)build_chain(index, 10);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    REQUIRE(index.active_tip_height() == 9);
    for (int32_t h = 0; h <= 9; ++h) {
        auto const idx = index.active_at(h);
        REQUIRE(idx != header_index::null_index);
        REQUIRE(index.get_height(idx) == h);
    }
    REQUIRE(index.active_at(10) == header_index::null_index);
}

TEST_CASE("active chain stays correct when a side branch shifts indices", "[header_organizer][active_chain]") {
    // The regression this structure exists for: the index numbers entries in
    // arrival order, so storing a side branch used to make index != height for
    // every later block, silently corrupting height-addressed lookups.
    header_index index;
    auto tip_hash = build_chain(index, 10);           // heights 0..9 at indices 0..9
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    // Store a 3-block side branch off height 5: it takes indices 10..12.
    auto branch = make_branch(index.get_hash(index.active_at(5)), 3, 800);
    (void)organizer.add_headers(branch);
    REQUIRE(index.size() == 13);

    // Now extend the MAIN chain: height 10 lands at index 13, not 10.
    domain::message::header::list extension;
    extension.push_back(make_header_with_prev(tip_hash, 10));
    auto result = organizer.add_headers(extension);
    REQUIRE(result.headers_added == 1);

    auto const idx10 = index.active_at(10);
    REQUIRE(idx10 != header_index::null_index);
    REQUIRE(idx10 != 10);                             // the index HAS shifted
    REQUIRE(index.get_height(idx10) == 10);           // but the mapping is right
    REQUIRE(index.active_tip_height() == 10);

    // The branch must not be on the active chain.
    for (int32_t h = 0; h <= 10; ++h) {
        REQUIRE(index.get_height(index.active_at(h)) == h);
    }
}

TEST_CASE("active chain re-points to a new tip across a fork", "[header_organizer][active_chain]") {
    header_index index;
    (void)build_chain(index, 10);                     // tip height 9
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    auto const old_idx7 = index.active_at(7);

    // A branch off height 5 that outgrows the current chain.
    auto branch = make_branch(index.get_hash(index.active_at(5)), 7, 900);
    (void)organizer.add_headers(branch);

    // Switch the active chain onto the branch head (what a reorg will do once
    // the abandoned blocks have been disconnected).
    auto const branch_head = index.find(kth::domain::chain::hash(branch.back()));
    REQUIRE(branch_head != header_index::null_index);
    index.active_set_tip(branch_head);

    REQUIRE(index.active_tip_height() == 12);         // 5 + 7
    REQUIRE(index.active_at(12) == branch_head);
    REQUIRE(index.active_at(7) != old_idx7);          // height 7 now on the branch
    // Everything at or below the fork is untouched.
    for (int32_t h = 0; h <= 5; ++h) {
        REQUIRE(index.get_height(index.active_at(h)) == h);
    }
}

TEST_CASE("chain generation moves only on a branch change", "[header_organizer][active_chain]") {
    // The generation stamp is what lets the block pipeline drop work downloaded
    // for an abandoned branch. It must NOT move on ordinary forward extension:
    // bumping it per block would discard perfectly good in-flight chunks on every
    // new tip, stalling sync instead of protecting it.
    header_index index;
    auto tip_hash = build_chain(index, 10);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    auto const initial = index.generation();

    // Forward extension: same branch, generation unchanged.
    domain::message::header::list extension;
    hash_digest prev = tip_hash;
    for (int i = 0; i < 3; ++i) {
        auto hdr = make_header_with_prev(prev, uint32_t(10 + i));
        prev = kth::domain::chain::hash(hdr);
        extension.push_back(std::move(hdr));
    }
    auto const extended = organizer.add_headers(extension);
    REQUIRE(extended.headers_added == 3);
    CHECK(index.generation() == initial);

    // Re-pointing onto a different branch does move it.
    auto branch = make_branch(index.get_hash(index.active_at(5)), 9, 1100);
    (void)organizer.add_headers(branch);
    auto const branch_head = index.find(kth::domain::chain::hash(branch.back()));
    REQUIRE(branch_head != header_index::null_index);
    index.active_set_tip(branch_head);

    CHECK(index.generation() == initial + 1);
}

TEST_CASE("the active chain is never observed in a torn state", "[header_organizer][active_chain]") {
    // active_set_tip truncates and then re-links, so it is NOT atomic: during a
    // switch the chain is transiently SHORT. That is safe by construction —
    // active_at() answers null_index for the heights not yet re-linked, so a
    // reader fails closed (block download logs a missing hash and skips) rather
    // than resolving a height to a block from the abandoned branch.
    //
    // What must hold at all times is that whatever active_at(h) DOES return is
    // an entry actually at height h on a branch descending from the fork. A
    // reader must never see an index left over from the previous chain sitting
    // at a height it does not belong to.
    //
    // Note on scope, since it would be easy to overclaim: this pins the absence
    // of torn entries. The ordering fix itself — publishing the chain before
    // bumping the generation — is not provable here, because the transient
    // truncation means neither "generation implies chain" nor its converse holds
    // during a switch. The consequence of the ordering is covered by the storage
    // drop test in the node suite.
    header_index index;
    (void)build_chain(index, 10);
    auto settings = make_test_settings();
    header_organizer organizer(index, settings, domain::config::network::mainnet);
    REQUIRE(organizer.start());
    organizer.sync_tip();

    auto const fork_height = 5;
    auto const fork_idx = index.active_at(fork_height);
    auto const fork_hash = index.get_hash(fork_idx);
    constexpr int switches = 64;

    std::vector<header_index::index_t> heads;
    for (int i = 0; i < switches; ++i) {
        auto const branch = make_branch(fork_hash, size_t(i + 1), uint32_t(4000 + i * 100));
        (void)organizer.add_headers(branch);
        auto const head = index.find(kth::domain::chain::hash(branch.back()));
        REQUIRE(head != header_index::null_index);
        heads.push_back(head);
    }

    std::atomic<bool> done{false};
    std::atomic<bool> violated{false};
    std::atomic<bool> reader_started{false};
    std::atomic<uint64_t> reads{0};

    std::thread reader([&] {
        reader_started.store(true, std::memory_order_release);
        while ( ! done.load(std::memory_order_acquire)) {
            auto const tip_height = index.active_tip_height();
            for (int32_t h = 0; h <= tip_height; ++h) {
                auto const idx = index.active_at(h);
                if (idx == header_index::null_index) continue;   // not linked yet: safe

                if (index.get_height(idx) != h) {
                    violated.store(true, std::memory_order_release);
                    return;
                }
                if (h > fork_height && index.get_ancestor(idx, fork_height) != fork_idx) {
                    violated.store(true, std::memory_order_release);
                    return;
                }
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    while ( ! reader_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    auto const base = index.generation();
    for (int i = 0; i < switches; ++i) {
        index.active_set_tip(heads[size_t(i)]);
    }
    done.store(true, std::memory_order_release);
    reader.join();

    CHECK_FALSE(violated.load());
    CHECK(reads.load() > 0);                            // the checks above ran
    CHECK(index.generation() == base + switches);       // every move counted

    // Quiesced, the chain and the counter agree on the final branch.
    CHECK(index.active_at(index.active_tip_height()) == heads.back());
}
