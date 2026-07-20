// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Rigorous correctness tests for the BCH mempool (kth::blockchain::mempool).
//
// Two layers:
//   1. Serial correctness — deterministic, single-threaded, asserts exact
//      expected states (first-seen, duplicate, confirm-keeps-descendants,
//      conflict-evicts-descendants-recursively, prevout overlay resolution).
//   2. Concurrent stress — N threads doing random add / remove_for_block /
//      resolve / contains over a deterministic key space that deliberately
//      contains conflicts and chains. After a quiescent barrier we check the
//      first-seen safety invariant and size/contained consistency via the
//      public API. Intended to be run under TSan / ASan+UBSan.

#include <test_helpers.hpp>

#include <kth/blockchain/pools/mempool.hpp>
#include <kth/domain.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <thread>
#include <vector>

using namespace kth;
using namespace kth::domain::chain;
using kth::blockchain::mempool;
using kth::blockchain::mempool_entry;

namespace {

// A distinct, deterministic 32-byte hash for a given seed. Synthetic prevout
// hashes never collide with real txids (which come out of double-SHA256).
hash_digest make_hash(uint64_t seed) {
    hash_digest h{};
    for (int i = 0; i < 8; ++i) {
        h[i] = static_cast<uint8_t>(seed >> (8 * i));
    }
    // Sprinkle a couple more bytes so seeds that differ only in high bits still
    // produce well-separated hashes.
    h[16] = static_cast<uint8_t>(seed >> 5);
    h[31] = static_cast<uint8_t>(seed >> 11);
    return h;
}

output_point op(uint64_t seed, uint32_t index) {
    return output_point{make_hash(seed), index};
}

// Build a syntactically-valid transaction whose inputs point at `prevouts` and
// which has `num_outputs` trivial outputs. `tag` makes otherwise-identical txs
// distinct (distinct outputs => distinct txid), so txs that deliberately share
// an input outpoint still get different txids.
transaction make_tx(std::vector<output_point> const& prevouts,
                    uint32_t num_outputs,
                    uint64_t tag) {
    input::list ins;
    ins.reserve(prevouts.size());
    for (auto const& po : prevouts) {
        ins.emplace_back(po, script{}, 0xffffffffu);
    }

    output::list outs;
    outs.reserve(num_outputs);
    for (uint32_t i = 0; i < num_outputs; ++i) {
        output o;
        o.set_value(1000u + tag * 100u + i);
        o.set_script(script{});
        outs.push_back(std::move(o));
    }

    return transaction{1u, 0u, std::move(ins), std::move(outs)};
}

transaction_const_ptr as_ptr(transaction const& tx) {
    return std::make_shared<domain::message::transaction>(tx);
}

mempool_entry entry_for(transaction_const_ptr const& tx, uint64_t tag) {
    return mempool_entry{
        tx,
        /*fee*/      1000u + tag,
        /*size*/     static_cast<uint32_t>(tx->serialized_size(true)),
        /*sigchecks*/ 1u,
        /*time_seen*/ tag};
}

mempool_entry entry_for(transaction const& tx, uint64_t tag) {
    return entry_for(as_ptr(tx), tag);
}

block make_block(std::vector<transaction> const& txs) {
    return block{header{}, transaction::list(txs.begin(), txs.end())};
}

} // namespace

// ---------------------------------------------------------------------------
// Serial correctness
// ---------------------------------------------------------------------------

TEST_CASE("mempool add then query", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    auto const tx = make_tx({op(1, 0)}, 2, /*tag*/ 1);
    auto const txid = tx.hash();

    REQUIRE(mp.add(entry_for(tx, 1)));
    REQUIRE(mp.contains(txid));
    REQUIRE(mp.size() == 1u);

    auto const r0 = mp.resolve(point{txid, 0});
    REQUIRE(r0.has_value());
    CHECK(r0->value() == tx.outputs()[0].value());

    auto const r1 = mp.resolve(point{txid, 1});
    REQUIRE(r1.has_value());
    CHECK(r1->value() == tx.outputs()[1].value());

    // Out-of-range output index -> nullopt.
    CHECK_FALSE(mp.resolve(point{txid, 2}).has_value());
    // Unknown txid -> nullopt.
    CHECK_FALSE(mp.resolve(point{make_hash(999), 0}).has_value());
}

TEST_CASE("mempool first-seen rejects conflicting input", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    auto const a = make_tx({op(1, 0)}, 1, 1);
    auto const b = make_tx({op(1, 0)}, 1, 2);  // spends the SAME outpoint as a
    REQUIRE(a.hash() != b.hash());

    REQUIRE(mp.add(entry_for(a, 1)));
    // b double-spends op(1,0), already claimed by a -> rejected, not admitted.
    REQUIRE_FALSE(mp.add(entry_for(b, 2)));
    CHECK(mp.contains(a.hash()));
    CHECK_FALSE(mp.contains(b.hash()));
    CHECK(mp.size() == 1u);
}

TEST_CASE("mempool first-seen conflict rolls back partial claims", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    // a spends P. b spends {Q, P}: Q is free, P is taken by a. b must be
    // rejected AND its speculative claim on Q must be rolled back, so a later
    // tx c spending Q can still be admitted.
    auto const P = op(1, 0);
    auto const Q = op(2, 0);
    auto const a = make_tx({P}, 1, 1);
    auto const b = make_tx({Q, P}, 1, 2);
    auto const c = make_tx({Q}, 1, 3);

    REQUIRE(mp.add(entry_for(a, 1)));
    REQUIRE_FALSE(mp.add(entry_for(b, 2)));
    // Q must be free again:
    REQUIRE(mp.add(entry_for(c, 3)));
    CHECK(mp.contains(a.hash()));
    CHECK(mp.contains(c.hash()));
    CHECK_FALSE(mp.contains(b.hash()));
    CHECK(mp.size() == 2u);
}

TEST_CASE("mempool rejects duplicate txid", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    auto const a = make_tx({op(1, 0)}, 1, 1);
    REQUIRE(mp.add(entry_for(a, 1)));
    // Same content => same txid => rejected on re-add. State unchanged.
    REQUIRE_FALSE(mp.add(entry_for(a, 1)));
    CHECK(mp.contains(a.hash()));
    CHECK(mp.size() == 1u);

    // A zero-input tx (exercises the pool_-duplicate path directly, since there
    // are no outpoints to conflict on).
    auto const z = make_tx({}, 1, 42);
    REQUIRE(mp.add(entry_for(z, 42)));
    REQUIRE_FALSE(mp.add(entry_for(z, 42)));
    CHECK(mp.size() == 2u);
}

TEST_CASE("mempool remove_for_block drops confirmed tx", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    auto const a = make_tx({op(1, 0)}, 1, 1);
    REQUIRE(mp.add(entry_for(a, 1)));
    REQUIRE(mp.size() == 1u);

    mp.remove_for_block(make_block({a}));
    CHECK_FALSE(mp.contains(a.hash()));
    CHECK(mp.size() == 0u);

    // The input outpoint must be released, so a different tx spending it is now
    // admissible (it was confirmed/on-chain but the pool index must be clean).
    auto const a2 = make_tx({op(1, 0)}, 1, 2);
    CHECK(mp.add(entry_for(a2, 2)));
}

TEST_CASE("mempool normal confirm keeps descendants", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    // A spends external P; B spends A's output {A,0}.
    auto const a = make_tx({op(1, 0)}, 1, 1);
    auto const b = make_tx({output_point{a.hash(), 0}}, 1, 2);

    REQUIRE(mp.add(entry_for(a, 1)));
    REQUIRE(mp.add(entry_for(b, 2)));
    REQUIRE(mp.size() == 2u);

    // A is confirmed in a block. Its output is now on-chain, so B stays valid
    // and MUST remain in the pool (non-recursive removal).
    mp.remove_for_block(make_block({a}));
    CHECK_FALSE(mp.contains(a.hash()));
    CHECK(mp.contains(b.hash()));
    CHECK(mp.size() == 1u);
}

TEST_CASE("mempool conflict evicts descendants recursively", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    // Chain A -> B -> C in the pool.
    auto const a = make_tx({op(1, 0)}, 1, 1);            // spends external P
    auto const b = make_tx({output_point{a.hash(), 0}}, 1, 2);     // spends {A,0}
    auto const c = make_tx({output_point{b.hash(), 0}}, 1, 3);     // spends {B,0}

    REQUIRE(mp.add(entry_for(a, 1)));
    REQUIRE(mp.add(entry_for(b, 2)));
    REQUIRE(mp.add(entry_for(c, 3)));
    REQUIRE(mp.size() == 3u);

    // A block tx X double-spends P (A's input) but is itself NOT in the pool.
    // A conflicts with X => A is invalid, and B and C (descendants) go with it.
    auto const x = make_tx({op(1, 0)}, 1, 99);
    REQUIRE(x.hash() != a.hash());

    mp.remove_for_block(make_block({x}));
    CHECK_FALSE(mp.contains(a.hash()));
    CHECK_FALSE(mp.contains(b.hash()));
    CHECK_FALSE(mp.contains(c.hash()));
    CHECK(mp.size() == 0u);
}

TEST_CASE("mempool resolve overlays chained parent output", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    auto const a = make_tx({op(1, 0)}, 2, 1);
    auto const b = make_tx({output_point{a.hash(), 1}}, 1, 2);  // spends A's output #1

    REQUIRE(mp.add(entry_for(a, 1)));
    REQUIRE(mp.add(entry_for(b, 2)));

    // Resolving B's parent outpoint {A,1} must return A's output #1.
    auto const r = mp.resolve(point{a.hash(), 1});
    REQUIRE(r.has_value());
    CHECK(r->value() == a.outputs()[1].value());

    // Unknown outpoint -> nullopt.
    CHECK_FALSE(mp.resolve(point{make_hash(123456), 0}).has_value());
}

TEST_CASE("mempool update_for_reorg evicts for incoming blocks", "[mempool][serial]") {
    mempool mp{0x1111u, 0x2222u};

    auto const a = make_tx({op(1, 0)}, 1, 1);
    auto const b = make_tx({output_point{a.hash(), 0}}, 1, 2);  // child of A
    auto const other = make_tx({op(5, 0)}, 1, 3);
    // X conflicts with A (same input P), not in the pool.
    auto const x = make_tx({op(1, 0)}, 1, 99);

    REQUIRE(mp.add(entry_for(a, 1)));
    REQUIRE(mp.add(entry_for(b, 2)));
    REQUIRE(mp.add(entry_for(other, 3)));
    REQUIRE(mp.size() == 3u);

    // Build incoming = one block containing X.
    auto blk = std::make_shared<domain::message::block>(
        header{}, transaction::list{x});
    auto list = std::make_shared<domain::message::block::const_ptr_list>();
    list->push_back(blk);
    block_const_ptr_list_const_ptr incoming = list;

    mp.update_for_reorg(nullptr, incoming);

    // A (and its descendant B) evicted by the conflict; `other` untouched.
    CHECK_FALSE(mp.contains(a.hash()));
    CHECK_FALSE(mp.contains(b.hash()));
    CHECK(mp.contains(other.hash()));
    CHECK(mp.size() == 1u);
}

// ---------------------------------------------------------------------------
// Concurrent stress
// ---------------------------------------------------------------------------

namespace {

// One candidate transaction the workers may add/remove/query.
struct candidate {
    transaction tx;
    hash_digest txid;
    transaction_const_ptr ptr;
    uint64_t tag;
    std::vector<std::pair<hash_digest, uint32_t>> inpoints;  // spent outpoints
};

// Deterministic key space with deliberate conflicts and chains:
//   - `groups` independent groups.
//   - Each group has a "root" set of txs that all spend from a small shared
//     pool of base outpoints -> many first-seen conflicts.
//   - Plus chained children that spend a root's output -> descendant chains.
std::vector<candidate> build_candidates(std::size_t groups) {
    std::vector<candidate> out;
    uint64_t tag = 1;

    for (std::size_t g = 0; g < groups; ++g) {
        uint64_t const base = 1'000'000ull * (g + 1);

        // 4 base outpoints shared within the group.
        std::vector<output_point> const bases = {
            op(base + 1, 0), op(base + 2, 0),
            op(base + 3, 0), op(base + 4, 0)};

        std::vector<hash_digest> roots;

        // 6 root txs, each spending 2 of the base outpoints in overlapping
        // combinations -> guaranteed conflicts among them.
        static constexpr int combos[6][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 2}, {1, 3}};
        for (auto const& combo : combos) {
            auto tx = make_tx({bases[combo[0]], bases[combo[1]]}, 2, tag);
            candidate c;
            c.txid = tx.hash();
            for (auto const& in : tx.inputs()) {
                auto const& o = in.previous_output();
                c.inpoints.emplace_back(o.hash(), o.index());
            }
            roots.push_back(c.txid);
            c.ptr = as_ptr(tx);
            c.tx = std::move(tx);
            c.tag = tag++;
            out.push_back(std::move(c));
        }

        // 6 children, each spending output 0 of a distinct root -> chains.
        for (auto const& root : roots) {
            auto tx = make_tx({output_point{root, 0}}, 1, tag);
            candidate c;
            c.txid = tx.hash();
            for (auto const& in : tx.inputs()) {
                auto const& o = in.previous_output();
                c.inpoints.emplace_back(o.hash(), o.index());
            }
            c.ptr = as_ptr(tx);
            c.tx = std::move(tx);
            c.tag = tag++;
            out.push_back(std::move(c));
        }
    }

    return out;
}

void run_concurrent(unsigned n_threads) {
    constexpr std::size_t groups = 24;
    constexpr int ops_per_thread = 40'000;

    auto const cands = build_candidates(groups);

    // Sanity: all candidate txids distinct (so size()==contained-count holds).
    {
        std::set<hash_digest> ids;
        for (auto const& c : cands) {
            ids.insert(c.txid);
        }
        REQUIRE(ids.size() == cands.size());
    }

    mempool mp{0xA5A5A5A5u, 0x5A5A5A5Au};

    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    threads.reserve(n_threads);

    for (unsigned t = 0; t < n_threads; ++t) {
        threads.emplace_back([&, t] {
            std::mt19937_64 rng(0xC0FFEEu + t);
            std::uniform_int_distribution<std::size_t> pick(0, cands.size() - 1);
            std::uniform_int_distribution<int> what(0, 99);

            while ( ! go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (int i = 0; i < ops_per_thread; ++i) {
                int const roll = what(rng);
                auto const& c = cands[pick(rng)];

                if (roll < 55) {
                    // add (fresh entry sharing the tx's shared_ptr)
                    mp.add(entry_for(c.ptr, c.tag));
                } else if (roll < 70) {
                    // remove_for_block of a small block containing this tx
                    mp.remove_for_block(make_block({c.tx}));
                } else if (roll < 85) {
                    // resolve one of this tx's outputs
                    (void)mp.resolve(point{c.txid, 0});
                } else {
                    // contains
                    (void)mp.contains(c.txid);
                }
            }
        });
    }

    go.store(true, std::memory_order_release);
    for (auto& th : threads) {
        th.join();
    }

    // ---- Quiescent invariant checks (public API only) ----

    // 1) size() must equal the number of candidates currently contained.
    std::size_t contained_count = 0;
    std::vector<candidate const*> contained;
    for (auto const& c : cands) {
        if (mp.contains(c.txid)) {
            ++contained_count;
            contained.push_back(&c);
        }
    }
    CHECK(mp.size() == contained_count);

    // 2) First-seen safety: no two currently-contained txs may spend the same
    //    outpoint.
    std::map<std::pair<hash_digest, uint32_t>, hash_digest> owner;
    bool disjoint = true;
    std::pair<hash_digest, uint32_t> clash_point{};
    for (auto const* c : contained) {
        for (auto const& ip : c->inpoints) {
            auto const [it, inserted] = owner.emplace(ip, c->txid);
            if ( ! inserted) {
                disjoint = false;
                clash_point = ip;
            }
        }
    }
    INFO("threads=" << n_threads
         << " contained=" << contained_count
         << " clash_index=" << clash_point.second);
    CHECK(disjoint);

    // 3) Every contained tx must resolve its output 0 (pool overlay consistent
    //    with membership).
    for (auto const* c : contained) {
        CHECK(mp.resolve(point{c->txid, 0}).has_value());
    }
}

} // namespace

TEST_CASE("mempool concurrent stress 8 threads", "[mempool][concurrent]") {
    run_concurrent(8);
}

TEST_CASE("mempool concurrent stress 16 threads", "[mempool][concurrent]") {
    run_concurrent(16);
}

TEST_CASE("mempool concurrent stress 32 threads", "[mempool][concurrent]") {
    run_concurrent(32);
}
