// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(KTH_CURRENCY_BCH)
#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <future>
#include <span>
#include <limits>
#include <stdexcept>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <optional>
#include <vector>

#include <kth/infrastructure/utility/timer.hpp>
#include <kth/blockchain/abla_pipeline.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/utxo_builder.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::blockchain;
using namespace kth::test;

// =============================================================================
// ABLA through the live connect pipeline (abla_pipeline.hpp)
// =============================================================================
//
// What these exercise is the production boundary, not the math: the state a
// connected batch derives from the persisted tip and the sizes of its blocks,
// what is written with the heights, what a restart reads back, and what the
// candidate and a side branch are described with. The expected values are
// computed here with abla::next directly, from first principles of the
// invariant (state OF block h = next(state OF block h-1, size_h, tau_h), the
// transition consuming the PREVIOUS state's size and interval), so a pipeline
// that advanced twice, never, from the wrong parent or at the wrong interval
// cannot agree with them.

namespace {

namespace abla = domain::chain::abla;
using domain::chain::block;

constexpr auto net = domain::config::network::regtest;
constexpr uint32_t h_a = 120;                  // first height whose own MTP reaches the activation

uint32_t base_time(uint32_t len) {
    // Recent enough that header organization does not call the chain stale.
    return uint32_t(zulu_time()) - (len + 30) * 600;
}

// MTP(h) = timestamp(h - 5) for h >= 10 on timestamps base + 600 * h, so
// MTP(h_a) is the first to reach this and MTP(h_a - 1) is one block short.
uint32_t activation_time(uint32_t base) {
    return base + (h_a - 5) * 600;
}

std::vector<block> mine_chain(hash_digest prev, uint32_t first, uint32_t count, uint32_t base,
                              uint32_t salt, size_t activation = max_size_t) {
    auto const ticks = activation == max_size_t ? domain::chain::block_ticks{}
        : domain::chain::block_ticks::activated_at(activation);
    std::vector<block> blocks;
    for (uint32_t h = first; h < first + count; ++h) {
        block candidate(domain::chain::header{0x20000000, prev, null_hash, base + h * 600, regtest_bits, 0},
            {coinbase_tx(h, salt, block::subsidy(ticks, h, false))});
        auto const merkle = candidate.generate_merkle_root();
        bool found = false;
        for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
            domain::chain::header header{0x20000000, prev, merkle, base + h * 600, regtest_bits, uint32_t(nonce)};
            if (header.is_valid_proof_of_work(domain::chain::hash(header), false)) {
                blocks.emplace_back(header, candidate.transactions());
                found = true;
                break;
            }
        }
        if (!found) throw std::runtime_error("no nonce satisfied the regtest target");
        prev = blocks.back().hash();
    }
    return blocks;
}

// Headers into the index and, optionally, the by-height table the restart reads.
void add_headers(chain_fixture& fixture, std::vector<block> const& blocks, uint32_t first, bool persist) {
    domain::message::header::list message_headers;
    domain::chain::header::list headers;
    for (auto const& blk : blocks) {
        message_headers.push_back(blk.header());
        headers.push_back(blk.header());
    }
    auto const added = fixture.organizer().add_headers(message_headers);
    CAPTURE(added.error.message(), first);
    if (persist) {
        REQUIRE(added.headers_added == blocks.size());
        REQUIRE( ! fixture.chain().organize_headers_batch(headers, first));
    } else {
        // Accepted side branches report no forward progress; verify actual admission.
        REQUIRE(added.error == error::stale_chain);
        for (auto const& block : blocks) REQUIRE(fixture.chain().headers().find(block.hash()) != header_index::null_index);
    }
}

// The reference. `parent` unknown (nullopt) starts the algorithm from its
// initial values, as the first block after a zero record does.
std::vector<abla::state> reference(abla::config const& cfg, std::optional<abla::state> parent,
                                   uint32_t first, std::vector<uint64_t> const& sizes,
                                   uint32_t (*tau)(uint32_t)) {
    std::vector<abla::state> out;
    for (size_t i = 0; i < sizes.size(); ++i) {
        auto const h = first + uint32_t(i);
        if (parent) {
            auto const next = abla::next(*parent, cfg, sizes[i], tau(h));
            REQUIRE(next.has_value());
            parent = *next;
        } else {
            parent = abla::state(cfg, sizes[i], tau(h));
        }
        out.push_back(*parent);
    }
    return out;
}

uint32_t tau_600(uint32_t) { return 600; }
uint32_t tau_faster(uint32_t h) { return h > h_a ? 60 : 600; }

// What dynamic_max_block_size() must answer for a candidate with this state.
uint64_t expected_limit(abla::state const& candidate, uint64_t tau) {
    auto const dynamic = abla::block_size_limit(candidate, tau);
    REQUIRE(dynamic.has_value());
    auto const floor = static_max_block_size(net) * tau / 600;
    return std::min<uint64_t>(std::max<uint64_t>(floor, *dynamic), static_absolute_max_block_size());
}

void check_same(abla::state const& got, abla::state const& want) {
    CHECK(got.block_size == want.block_size);
    CHECK(got.control_block_size == want.control_block_size);
    CHECK(got.elastic_buffer_size == want.elastic_buffer_size);
    CHECK(got.block_interval == want.block_interval);
}

// Drive ABLA with the sizes selected by each scenario. Also connect the real
// coinbase-only bodies: output deltas, genuine empty undo records and durability
// barriers exist before publishing a connected height. No checkpoint bypass.
std::vector<abla::state> connect(chain_fixture& fixture, settings const& s, uint32_t first,
    std::vector<uint64_t> const& sizes, std::span<block const> blocks) {
    REQUIRE(blocks.size() == sizes.size());
    REQUIRE_FALSE(blocks.empty());
    auto& chain = fixture.chain();
    auto const last = chain.headers().find(blocks.back().hash());
    REQUIRE(last != header_index::null_index);
    auto states = abla_states_for_batch(chain, s, net, first, sizes, last);
    REQUIRE(states);
    auto const tip = first + uint32_t(sizes.size()) - 1;
    auto const records = abla_records_for(first, *states);
    std::vector<data_chunk> raw;
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    raw.reserve(blocks.size());
    for (auto const& block : blocks) {
        REQUIRE(block.transactions().size() == 1);
        REQUIRE(block.transactions().front().is_coinbase());
        raw.emplace_back(block.serialized_size());
        byte_writer writer(raw.back());
        REQUIRE(block.to_data(writer));
        byte_reader reader(raw.back());
        auto parsed = domain::chain::light_block::from_data(reader, true);
        REQUIRE(parsed);
        light.push_back(std::make_shared<domain::chain::light_block const>(std::move(*parsed)));
    }
    ::asio::io_context io;
    auto stored = ::asio::co_spawn(io, chain.store_chunk(light, first), ::asio::use_future);
    io.run();
    REQUIRE_FALSE(stored.get());
    struct undo_entry { header_index::index_t idx; database::block_undo undo; hash_digest parent; };
    std::vector<undo_entry> undo;
    utxo_raw_delta delta;
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto const height = first + uint32_t(i);
        auto const idx = chain.headers().find(blocks[i].hash());
        REQUIRE(idx != header_index::null_index);
        std::vector<uint32_t> timestamps;
        auto previous = chain.headers().get_parent_index(idx);
        for (size_t n = 0; previous != header_index::null_index && n < 11; ++n) {
            timestamps.push_back(chain.headers().get_timestamp(previous));
            previous = chain.headers().get_parent_index(previous);
        }
        std::sort(timestamps.begin(), timestamps.end());
        REQUIRE_FALSE(timestamps.empty());
        auto parsed = parse_utxo_block(raw[i]);
        REQUIRE(parsed);
        auto block_delta = process_compact_block_utxos(*parsed, blocks[i].hash(), height,
            timestamps[timestamps.size() / 2], net, chain.headers().get_file_number(idx),
            chain.headers().get_data_pos(idx));
        REQUIRE(block_delta);
        REQUIRE(block_delta->deletes.empty());
        auto captured = capture_block_undo(*block_delta, delta, chain, height);
        REQUIRE(captured);
        REQUIRE(captured->spent.empty());
        undo.push_back({idx, std::move(*captured), blocks[i].header().previous_block_hash()});
        REQUIRE(delta.merge(std::move(*block_delta)) == delta_merge_result::ok);
    }
    REQUIRE(chain.begin_transition());
    struct reopen { block_chain& chain; ~reopen() { chain.end_transition(); } } guard{chain};
    REQUIRE(chain.begin_transition_record(database::utxo_transition_record{
        .operation_id = database::make_operation_id(), .first_height = first,
        .intended_last_height = tip}) == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);
    {
        auto window = chain.begin_utxo_write();
        REQUIRE(window);
        window->mark_mutating();
        REQUIRE(apply_utxo_delta(chain, *window, delta, tip).ok());
        std::vector<int32_t> files;
        for (auto const& entry : undo) {
            auto file = chain.store_block_undo(entry.idx, entry.undo, entry.parent);
            REQUIRE(file);
            files.push_back(*file);
        }
        REQUIRE(chain.flush_undo(files));
        REQUIRE(chain.utxo_sync(*window) != database::barrier_outcome::failed);
        window->complete();
    }
    REQUIRE(chain.publish_transition(
        database::transition_heights{.last_block_height = tip, .utxo_built_height = tip},
        records) == database::result_code::success);
    REQUIRE(chain.env_sync() == database::result_code::success);
    return *states;
}

} // namespace

// -----------------------------------------------------------------------------
// A state above the floor survives connection and restart
// -----------------------------------------------------------------------------

TEST_CASE("abla pipeline - the candidate is one advance past the persisted tip, above the floor",
          "[abla][faster_blocks][pipeline]") {
    constexpr uint32_t len = 40;
    auto const base = base_time(len);
    chain_fixture fixture("abla_above_floor");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& s = fixture.chain().chain_settings();
    auto const& cfg = s.abla_config;

    auto const blocks = mine_chain(block::genesis_regtest().hash(), 1, len, base, 0);
    add_headers(fixture, blocks, 1, true);

    // Full blocks, so the control block size climbs away from its floor.
    std::vector<uint64_t> const sizes(len, cfg.epsilon0 + cfg.beta0);
    auto const states = connect(fixture, s, 1, sizes, blocks);
    auto const want = reference(cfg, std::nullopt, 1, sizes, tau_600);
    REQUIRE(states.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        check_same(states[i], want[i]);
    }
    REQUIRE(states.back().control_block_size > cfg.epsilon0);      // genuinely above the floor

    // The candidate: ONE advance from S_tip, size unknown.
    auto const candidate = abla::next(want.back(), cfg, 0, 600);
    REQUIRE(candidate.has_value());
    auto const limit = expected_limit(*candidate, 600);
    REQUIRE(limit > static_max_block_size(net));                   // above the floor ...
    REQUIRE(limit != abla::block_size_limit(want.back()));         // ... and not S_tip's own limit

    auto& chain = fixture.chain();
    REQUIRE(chain.publish_chain_view(len) == error::success);
    CHECK(chain.chain_state()->dynamic_max_block_size() == limit);
    CHECK(chain.chain_state()->dynamic_max_block_sigchecks() == limit / block_maxbytes_maxsigchecks_ratio);
    check_same(chain.chain_state()->abla_state(), *candidate);

    // And a restart reads the same state back from disk: nothing in memory
    // survives, so this is what was written with the heights.
    REQUIRE(fixture.restart());
    auto const restored = fixture.chain().chain_state();
    REQUIRE(restored);
    CHECK(restored->height() == len + 1);
    CHECK(restored->dynamic_max_block_size() == limit);
    check_same(restored->abla_state(), *candidate);
}

TEST_CASE("abla pipeline - the persisted record is exactly the state of the block at that height",
          "[abla][faster_blocks][pipeline]") {
    constexpr uint32_t len = 12;
    auto const base = base_time(len);
    chain_fixture fixture("abla_record");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& s = fixture.chain().chain_settings();
    auto const& cfg = s.abla_config;

    auto const blocks = mine_chain(block::genesis_regtest().hash(), 1, len, base, 0);
    add_headers(fixture, blocks, 1, true);

    std::vector<uint64_t> sizes;
    for (uint32_t i = 0; i < len; ++i) sizes.push_back((cfg.epsilon0 + cfg.beta0) / (i % 3 + 1));
    auto const states = connect(fixture, s, 1, sizes, blocks);

    for (uint32_t h = 1; h <= len; ++h) {
        auto const row = fixture.chain().get_header_and_abla_state(h);
        REQUIRE(row.has_value());
        CHECK(std::get<1>(*row) == states[h - 1].block_size);
        CHECK(std::get<2>(*row) == states[h - 1].control_block_size);
        CHECK(std::get<3>(*row) == states[h - 1].elastic_buffer_size);
        // The header bytes are untouched by the rewrite.
        CHECK(domain::chain::hash(std::get<0>(*row)) == blocks[h - 1].hash());
    }
}

TEST_CASE("abla pipeline - a batch continues from the connected tip rather than restarting",
          "[abla][faster_blocks][pipeline]") {
    constexpr uint32_t len = 30;
    auto const base = base_time(len);
    chain_fixture fixture("abla_two_batches");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& s = fixture.chain().chain_settings();
    auto const& cfg = s.abla_config;

    auto const blocks = mine_chain(block::genesis_regtest().hash(), 1, len, base, 0);
    add_headers(fixture, blocks, 1, true);

    std::vector<uint64_t> const sizes(len, cfg.epsilon0 + cfg.beta0);
    // Two batches, the second reading its parent back from the first's records.
    connect(fixture, s, 1, std::vector<uint64_t>(sizes.begin(), sizes.begin() + 15), std::span(blocks).first(15));
    auto const second = connect(fixture, s, 16, std::vector<uint64_t>(sizes.begin() + 15, sizes.end()),
        std::span(blocks).subspan(15));

    auto const want = reference(cfg, std::nullopt, 1, sizes, tau_600);
    for (size_t i = 0; i < second.size(); ++i) {
        check_same(second[i], want[15 + i]);
    }
}

// -----------------------------------------------------------------------------
// The first 60-tick transition
// -----------------------------------------------------------------------------

TEST_CASE("abla pipeline - the transition into the first 60-tick block consumes the 600-tick state",
          "[abla][faster_blocks][pipeline]") {
    constexpr uint32_t len = h_a + 3;
    auto const base = base_time(len);
    chain_fixture fixture("abla_first_60");
    fixture.settings().cantor_activation_time = activation_time(base);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& s = fixture.chain().chain_settings();
    auto const& cfg = s.abla_config;

    auto const blocks = mine_chain(block::genesis_regtest().hash(), 1, len, base, 0, h_a);
    add_headers(fixture, blocks, 1, true);

    // Full blocks at either rate: 600-tick blocks up to H_a, a tenth of that after.
    std::vector<uint64_t> sizes;
    for (uint32_t h = 1; h <= len; ++h) sizes.push_back((cfg.epsilon0 + cfg.beta0) / (h > h_a ? 10 : 1));
    auto const states = connect(fixture, s, 1, sizes, blocks);
    auto const want = reference(cfg, std::nullopt, 1, sizes, tau_faster);
    REQUIRE(states.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        check_same(states[i], want[i]);
    }

    // The boundary, stated on its own.
    auto const& at_ha = states[h_a - 1];
    auto const& after = states[h_a];
    CHECK(at_ha.block_interval == 600);
    CHECK(after.block_interval == 60);
    // The transition responds to H_a's size at H_a's own interval. Taking the
    // output interval instead is the wrong convention and gives another state.
    auto wrong_parent = at_ha;
    wrong_parent.block_interval = 60;
    auto const wrong = abla::next(wrong_parent, cfg, sizes[h_a], 60);
    REQUIRE(wrong.has_value());
    CHECK((wrong->control_block_size != after.control_block_size ||
           wrong->elastic_buffer_size != after.elastic_buffer_size));

    // The candidate after a 60-tick tip is a 60-tick block: a tenth of the limit,
    // never reset to the floor and never the unscaled 600-tick limit.
    auto const candidate = abla::next(want.back(), cfg, 0, 60);
    REQUIRE(candidate.has_value());
    auto const limit = expected_limit(*candidate, 60);
    auto& chain = fixture.chain();
    REQUIRE(chain.publish_chain_view(len) == error::success);
    CHECK(chain.chain_state()->block_interval() == 60);
    CHECK(chain.chain_state()->dynamic_max_block_size() == limit);

    REQUIRE(fixture.restart());
    CHECK(fixture.chain().chain_state()->block_interval() == 60);
    CHECK(fixture.chain().chain_state()->dynamic_max_block_size() == limit);
    check_same(fixture.chain().chain_state()->abla_state(), *candidate);
}

// -----------------------------------------------------------------------------
// Reorganization: the ancestor's state, not the active chain's
// -----------------------------------------------------------------------------

TEST_CASE("abla pipeline - a reorganized branch continues from its fork point and restarts to the same limit",
          "[abla][faster_blocks][pipeline][reorg]") {
    constexpr uint32_t len_a = 24;
    constexpr uint32_t fork = 18;
    constexpr uint32_t len_b = 4;                 // fork + len_b < len_a: the side branch never becomes active
    auto const base = base_time(len_a);
    chain_fixture fixture("abla_reorg");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& s = fixture.chain().chain_settings();
    auto const& cfg = s.abla_config;
    auto& chain = fixture.chain();

    auto const a = mine_chain(block::genesis_regtest().hash(), 1, len_a, base, 0);
    add_headers(fixture, a, 1, true);
    std::vector<uint64_t> const sizes_a(len_a, cfg.epsilon0 + cfg.beta0);
    auto const states_a = connect(fixture, s, 1, sizes_a, a);

    // The competing branch, from the block at `fork`, with small blocks that pull
    // the control block size the other way.
    auto const b = mine_chain(a[fork - 1].hash(), fork + 1, len_b, base, 1);
    add_headers(fixture, b, fork + 1, false);

    // Rewind actual outputs through their persisted undo before adopting the
    // other branch. No producers run concurrently in this fixture.
    auto const branch_tip = chain.headers().find(b.back().hash());
    auto const switched = chain.switch_to_branch(branch_tip, fork);
    REQUIRE(switched.ok);
    REQUIRE(switched.validated_tip == fork);
    fixture.organizer().adopt_tip(branch_tip);
    domain::chain::header::list b_headers;
    for (auto const& blk : b) b_headers.push_back(blk.header());
    REQUIRE( ! chain.replace_headers_from(b_headers, fork + 1));

    std::vector<uint64_t> const sizes_b(len_b, (cfg.epsilon0 + cfg.beta0) / 8);
    auto const states_b = connect(fixture, s, fork + 1, sizes_b, b);

    // Continues from S_fork: exactly the branch's own chain of states ...
    auto const want_b = reference(cfg, states_a[fork - 1], fork + 1, sizes_b, tau_600);
    REQUIRE(states_b.size() == want_b.size());
    for (size_t i = 0; i < want_b.size(); ++i) {
        check_same(states_b[i], want_b[i]);
    }
    // ... which is not what continuing from the abandoned tip would give.
    auto const from_abandoned_tip = abla::next(states_a.back(), cfg, sizes_b[0], 600);
    REQUIRE(from_abandoned_tip.has_value());
    CHECK(from_abandoned_tip->control_block_size != states_b[0].control_block_size);

    // Restart: the index is rebuilt from the replaced table, so the node comes
    // back on the new branch with its state, and the limit is the branch's.
    auto const candidate = abla::next(want_b.back(), cfg, 0, 600);
    REQUIRE(candidate.has_value());
    REQUIRE(fixture.restart());
    auto const restored = fixture.chain().chain_state();
    REQUIRE(restored);
    CHECK(restored->height() == fork + len_b + 1);
    CHECK(restored->dynamic_max_block_size() == expected_limit(*candidate, 600));
    check_same(restored->abla_state(), *candidate);
}

TEST_CASE("abla pipeline - a side branch is described from its ancestor, not from the active pool",
          "[abla][faster_blocks][pipeline][reorg]") {
    constexpr uint32_t len = 24;
    constexpr uint32_t fork = 12;
    auto const base = base_time(len);
    chain_fixture fixture("abla_side_branch");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto const& s = fixture.chain().chain_settings();
    auto const& cfg = s.abla_config;
    auto& chain = fixture.chain();

    auto const a = mine_chain(block::genesis_regtest().hash(), 1, len, base, 0);
    add_headers(fixture, a, 1, true);
    std::vector<uint64_t> const sizes(len, cfg.epsilon0 + cfg.beta0);
    auto const states = connect(fixture, s, 1, sizes, a);
    REQUIRE(chain.publish_chain_view(len) == error::success);

    // Two blocks forking at `fork`. The pool (published) state is the active
    // chain's candidate at len + 1; the branch must not inherit from it.
    auto const b = mine_chain(a[fork - 1].hash(), fork + 1, 2, base, 1);
    auto side = std::make_shared<branch>(fork, chain.block_validations());
    auto const second = std::make_shared<domain::message::block const>(b[1]);
    auto const first = std::make_shared<domain::message::block const>(b[0]);
    REQUIRE(side->push_front(second));
    REQUIRE(side->push_front(first));

    auto const state = chain.chain_state(side);
    REQUIRE(state);
    REQUIRE(state->height() == fork + 2);

    // S_{fork+2}: the record at the fork advanced through the two blocks' own
    // (serialized) sizes.
    auto const want = reference(cfg, states[fork - 1], fork + 1,
        {first->serialized_size(1), second->serialized_size(1)}, tau_600);
    check_same(state->abla_state(), want.back());
    CHECK(state->abla_state().control_block_size != chain.chain_state()->abla_state().control_block_size);
}

TEST_CASE("chipnet's first ABLA candidate consumes the full activation parent", "[abla][activation_boundary]") {
    constexpr auto chipnet = domain::config::network::chipnet;
    constexpr uint32_t candidate_height = chipnet_lobachevski_activation_height;
    constexpr uint32_t parent_height = candidate_height - 1;
    chain_fixture fixture("abla_first_upgrade", chipnet);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    auto& chain = fixture.chain();
    auto const& cfg = fixture.settings().abla_config;

    // Trusted history models an already connected chipnet tip. No PoW/body
    // validation is under test: retain all ancestor headers in the real index
    // for MTP and ASERT, but persist only the full parent's header/state. This
    // fixture is not restarted and does not claim a body or undo history.
    auto previous = block::genesis_chipnet().hash();
    auto const first_time = block::genesis_chipnet().header().timestamp();
    domain::chain::header tip;
    for (uint32_t h = 1; h <= parent_height; ++h) {
        tip = domain::chain::header{0x20000000u, previous, null_hash,
            first_time + h * 600u, 0x1d00ffffu, h};
        previous = domain::chain::hash(tip);
        auto const added = chain.headers().add(previous, tip);
        REQUIRE(added.inserted);
        chain.headers().active_push(added.index);
    }
    REQUIRE(chain.headers().active_tip_height() == parent_height);
    REQUIRE_FALSE(chain.replace_headers_from({tip}, parent_height));

    abla::state const parent(cfg, cfg.epsilon0 + cfg.beta0, 600);
    std::vector<database::header_abla_record> records{{parent_height, parent.block_size,
        parent.control_block_size, parent.elastic_buffer_size}};
    REQUIRE(chain.publish_transition(database::transition_heights{
        .last_block_height = parent_height, .utxo_built_height = parent_height}, records)
        == database::result_code::success);
    auto const expected = abla::next(parent, cfg, 0, 600);
    REQUIRE(expected.has_value());
    REQUIRE(abla::block_size_limit(*expected) > cfg.epsilon0 + cfg.beta0);

    // Exercise the public publication/candidate boundary, not advance's helper.
    REQUIRE(chain.begin_transition());
    auto const published = chain.publish_chain_view(parent_height);
    chain.end_transition();
    REQUIRE_FALSE(published);
    auto const view = chain.chain_view();
    REQUIRE(view);
    REQUIRE(view->state->height() == candidate_height);
    CHECK(view->state->abla_state().control_block_size == expected->control_block_size);
    CHECK(view->state->abla_state().elastic_buffer_size == expected->elastic_buffer_size);
    CHECK(view->state->dynamic_max_block_size() == abla::block_size_limit(*expected));
}

#endif // KTH_CURRENCY_BCH
