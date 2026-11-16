// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(KTH_CURRENCY_BCH)
#include <test_helpers.hpp>

#include <algorithm>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>

#include <kth/blockchain/abla_pipeline.hpp>
#include <kth/blockchain/utxo_builder.hpp>
#include <kth/blockchain/validate/batch_validate.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/infrastructure/utility/timer.hpp>

#include "regtest_miner.hpp"
#include "reorg_chain_fixture.hpp"

using namespace kth;
using namespace kth::test;
namespace chain = kth::domain::chain;

namespace {

// The block path must enforce contextual transaction rules independently of
// mempool admission. Mine real regtest blocks and sign every spend over its
// actual version and token data; never call accept() in place of the batch.
struct batch_chain {
    std::vector<block_const_ptr> blocks;
    uint32_t const base_time = uint32_t(zulu_time()) - 140 * 600;

    void append(std::vector<chain::transaction> txs = {}, uint64_t fees = 0) {
        auto const height = uint32_t(blocks.size()) + 1;
        auto const prev = blocks.empty() ? chain::block::genesis_regtest().hash() : blocks.back()->hash();
        auto block = mine_block(prev, height, base_time + height * 600, 0, std::move(txs), fees);
        blocks.push_back(std::make_shared<domain::message::block const>(std::move(block)));
    }

    batch_chain() {
        for (uint32_t h = 1; h <= 101; ++h) append();
    }

    chain::transaction const& coinbase_one() const { return blocks.front()->transactions().front(); }

    void replace_tip_coinbase(uint32_t version, chain::token_data_opt token = {},
                              std::optional<chain::script> locking = {}) {
        auto const& original = *blocks.back();
        auto const& cb = original.transactions().front();
        auto outputs = chain::output::list(cb.outputs());
        outputs[0] = chain::output(outputs[0].value(), locking.value_or(outputs[0].script()), std::move(token));
        chain::transaction::list txs(original.transactions());
        txs.front() = chain::transaction(version, cb.locktime(), chain::input::list(cb.inputs()), std::move(outputs));
        chain::block candidate(original.header(), std::move(txs));
        auto const root = candidate.generate_merkle_root();
        for (uint64_t nonce = 0; nonce <= std::numeric_limits<uint32_t>::max(); ++nonce) {
            chain::header header{original.header().version(), original.header().previous_block_hash(),
                root, original.header().timestamp(), regtest_bits, uint32_t(nonce)};
            if (header.is_valid_proof_of_work(chain::hash(header), false)) {
                blocks.back() = std::make_shared<domain::message::block const>(chain::block(
                    std::move(header), chain::transaction::list(candidate.transactions())));
                return;
            }
        }
        throw std::runtime_error("no nonce satisfied the regtest target");
    }
};

chain::transaction signed_spend(chain::transaction const& previous, chain::output::list outputs,
                                domain::script_flags_t flags, uint32_t version = 2) {
    auto const outpoint = chain::output_point{previous.hash(), 0};
    chain::input::list inputs;
    inputs.emplace_back(outpoint, chain::script{}, 0xffffffff);
    chain::transaction unsigned_tx(version, 0, std::move(inputs), chain::output::list(outputs));
    unsigned_tx.inputs()[0].previous_output().validation.cache = previous.outputs()[0];
    auto const endorsement = chain::script::create_endorsement(
        miner_secret(), miner_script(), unsigned_tx, 0,
        uint8_t(infrastructure::machine::sighash_algorithm::forkid_all), flags, previous.outputs()[0].value());
    REQUIRE(endorsement);
    chain::input::list signed_inputs;
    signed_inputs.emplace_back(outpoint,
        chain::script(chain::script::to_pay_public_key_hash_pattern_unlocking(*endorsement, miner_public())),
        0xffffffff);
    return chain::transaction(version, 0, std::move(signed_inputs), std::move(outputs));
}

struct verdict {
    code result;
    blockchain::batch_rejection rejection;
};

verdict validate(char const* tag, batch_chain const& batch, blockchain::settings settings) {
    chain_fixture fixture(tag, settings);
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    domain::message::header::list headers;
    for (auto const& block : batch.blocks) headers.push_back(block->header());
    REQUIRE(fixture.organizer().add_headers(headers).headers_added == batch.blocks.size());

    // Prove that the real contextual state, not just the signing flags, has
    // the intended upgrade active at the candidate block's height.
    auto const& candidate = *batch.blocks.back();
    auto const parent = fixture.chain().headers().find(candidate.header().previous_block_hash());
    blockchain::validate_header validator(settings, domain::config::network::regtest);
    auto state = validator.chain_state_at(candidate.header(), candidate.hash(), batch.blocks.size(),
        parent, fixture.chain().headers());
    REQUIRE(state);
    REQUIRE((*state)->is_enabled(domain::machine::script_flags::bch_tokens) == settings.bch_descartes);

    verdict answer{error::success, {}};
    answer.result = blockchain::validate_block_batch(fixture.chain(), settings,
        domain::config::network::regtest, batch.blocks, 1, answer.rejection);
    return answer;
}

chain::output token_output(hash_digest const& category, int64_t amount, uint64_t satoshis) {
    return chain::output(satoshis, miner_script(),
        chain::token_data_opt{chain::make_fungible(category, amount)});
}

// Exercise the production raw serializer and UTXO-Z writer, including real
// flat-file positions in reference mode. Snapshot seeding inserts surviving
// outputs only; a connected batch also applies its deletions.
void store_outputs(chain_fixture& fixture, std::vector<block_const_ptr> const& blocks,
                   uint32_t first, domain::config::network network, bool connected) {
    auto& chain = fixture.chain();
    std::vector<data_chunk> raw;
    std::vector<std::shared_ptr<domain::chain::light_block const>> light;
    for (auto const& block : blocks) {
        raw.push_back(to_data_chunk(static_cast<chain::block const&>(*block)));
        byte_reader reader(raw.back());
        auto parsed = domain::chain::light_block::from_data(reader, true);
        REQUIRE(parsed);
        light.push_back(std::make_shared<domain::chain::light_block const>(std::move(*parsed)));
    }
    ::asio::io_context io;
    auto stored = ::asio::co_spawn(io, chain.store_chunk(light, first), ::asio::use_future);
    io.run();
    REQUIRE_FALSE(stored.get());
    blockchain::utxo_raw_delta delta;
    struct pending_undo {
        blockchain::header_index::index_t idx;
        database::block_undo undo;
        hash_digest parent;
    };
    std::vector<pending_undo> undo;
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto const idx = chain.headers().find(blocks[i]->hash());
        REQUIRE(idx != blockchain::header_index::null_index);
        auto parsed = blockchain::parse_utxo_block(raw[i]);
        REQUIRE(parsed);
        auto next = blockchain::process_compact_block_utxos(*parsed, blocks[i]->hash(),
            first + uint32_t(i), 0, network, chain.headers().get_file_number(idx),
            chain.headers().get_data_pos(idx));
        REQUIRE(next);
        if (connected) {
            auto captured = blockchain::capture_block_undo(*next, delta, chain, first + uint32_t(i));
            REQUIRE(captured);
            undo.push_back({idx, std::move(*captured), blocks[i]->header().previous_block_hash()});
        }
        REQUIRE(delta.merge(std::move(*next)) == blockchain::delta_merge_result::ok);
    }
    auto window = chain.begin_utxo_write();
    REQUIRE(window);
    window->mark_mutating();
    if (connected) {
        REQUIRE(blockchain::apply_utxo_delta(chain, *window, delta,
            first + uint32_t(blocks.size()) - 1).ok());
    } else {
        REQUIRE(chain.apply_utxo_inserts_raw(*window, delta.inserts) == database::result_code::success);
    }
    std::vector<int32_t> undo_files;
    for (auto const& entry : undo) {
        auto file = chain.store_block_undo(entry.idx, entry.undo, entry.parent);
        REQUIRE(file);
        undo_files.push_back(*file);
    }
    REQUIRE(chain.flush_undo(undo_files));
    REQUIRE(chain.utxo_sync(*window) != database::barrier_outcome::failed);
    window->complete();
}

} // namespace

TEST_CASE("batch contextual consensus: transaction versions before and after Descartes (BCHN: Upgrade 9)",
          "[batch_contextual_consensus][tx_version]") {
    for (bool active : {false, true}) {
        for (uint32_t version : {0u, 1u, 2u, 3u}) {
            DYNAMIC_SECTION("Descartes (BCHN: Upgrade 9)=" << active << ", version=" << version) {
                blockchain::settings settings(domain::config::network::regtest);
                settings.bch_descartes = active;
                settings.bch_lobachevski = active;
                settings.bch_galois = active;
                settings.bch_leibniz = active;
                batch_chain batch;
                auto spend = signed_spend(batch.coinbase_one(),
                    chain::output::list{chain::output(1'000'000, miner_script(), chain::token_data_opt{})},
                    settings.enabled_flags(), version);
                batch.append({std::move(spend)}, batch.coinbase_one().outputs()[0].value() - 1'000'000);
                auto const answer = validate("batch_tx_version", batch, settings);
                if (active && (version < 1 || version > 2)) {
                    CHECK(answer.result == error::invalid_tx_version);
                    CHECK(answer.rejection.block == std::optional<size_t>{101});
                } else {
                    CHECK(answer.result == error::success);
                    CHECK_FALSE(answer.rejection.block);
                }
            }
        }
    }
}

TEST_CASE("batch contextual consensus: a transfer cannot create fungible tokens",
          "[batch_contextual_consensus][tokens]") {
    for (int64_t amount : {1, 2}) {
        DYNAMIC_SECTION("one token spent into " << amount) {
            blockchain::settings settings(domain::config::network::regtest);
            batch_chain batch;
            auto const category = batch.coinbase_one().hash();
            auto genesis = signed_spend(batch.coinbase_one(),
                chain::output::list{token_output(category, 1, 1'000'000)}, settings.enabled_flags());
            batch.append({genesis}, batch.coinbase_one().outputs()[0].value() - 1'000'000);
            auto spend = signed_spend(genesis,
                chain::output::list{token_output(category, amount, 999'000)}, settings.enabled_flags());
            batch.append({std::move(spend)}, 1000);
            auto const answer = validate("batch_token_conservation", batch, settings);
            if (amount == 2) {
                CHECK(answer.result == error::token_fungible_insufficient);
                CHECK(answer.rejection.block == std::optional<size_t>{102});
            } else {
                CHECK(answer.result == error::success);
                CHECK_FALSE(answer.rejection.block);
            }
        }
    }
}
TEST_CASE("batch contextual consensus: coinbase versions follow Descartes (BCHN: Upgrade 9)",
          "[batch_contextual_consensus][tx_version][coinbase]") {
    for (uint32_t version : {0u, 1u, 2u, 3u}) {
        DYNAMIC_SECTION("coinbase version=" << version) {
            blockchain::settings settings(domain::config::network::regtest);
            batch_chain batch;
            batch.replace_tip_coinbase(version);
            auto const answer = validate("batch_coinbase_version", batch, settings);
            if (version < 1 || version > 2) {
                CHECK(answer.result == error::invalid_tx_version);
                CHECK(answer.rejection.block == std::optional<size_t>{100});
            } else {
                CHECK(answer.result == error::success);
                CHECK_FALSE(answer.rejection.block);
            }
        }
    }
}

TEST_CASE("batch contextual consensus: coinbases cannot mint CashTokens",
          "[batch_contextual_consensus][tokens][coinbase]") {
    blockchain::settings settings(domain::config::network::regtest);
    batch_chain batch;
    batch.replace_tip_coinbase(2, chain::token_data_opt{
        chain::make_fungible(batch.coinbase_one().hash(), 1)});
    auto const answer = validate("batch_coinbase_tokens", batch, settings);
    CHECK(answer.result == error::token_coinbase_has_tokens);
    CHECK(answer.rejection.block == std::optional<size_t>{100});
}
TEST_CASE("batch contextual consensus: persisted tokens survive the next batch and restart",
          "[batch_contextual_consensus][tokens][store]") {
    for (int64_t amount : {1, 2}) {
        DYNAMIC_SECTION("stored token spent into " << amount) {
            blockchain::settings settings(domain::config::network::regtest);
            chain_fixture fixture("batch_stored_token", settings);
            REQUIRE(fixture.created());
            REQUIRE(fixture.start());
            batch_chain batch;
            auto const category = batch.coinbase_one().hash();
            auto genesis = signed_spend(batch.coinbase_one(),
                chain::output::list{token_output(category, 1, 1'000'000)}, settings.enabled_flags());
            batch.append({genesis}, batch.coinbase_one().outputs()[0].value() - 1'000'000);
            domain::message::header::list headers;
            std::vector<uint64_t> sizes;
            for (auto const& block : batch.blocks) {
                headers.push_back(block->header());
                sizes.push_back(block->serialized_size(0));
            }
            REQUIRE(fixture.organizer().add_headers(headers).headers_added == batch.blocks.size());
            domain::chain::header::list persisted_headers;
            for (auto const& h : headers) persisted_headers.push_back(h);
            REQUIRE_FALSE(fixture.chain().organize_headers_batch(persisted_headers, 1));
            REQUIRE_FALSE(blockchain::validate_block_batch(fixture.chain(), settings,
                domain::config::network::regtest, batch.blocks, 1));
            auto const tip = fixture.chain().headers().find(batch.blocks.back()->hash());
            auto states = blockchain::abla_states_for_batch(fixture.chain(), settings,
                domain::config::network::regtest, 1, sizes, tip);
            REQUIRE(states);
            store_outputs(fixture, batch.blocks, 1, domain::config::network::regtest, true);
            REQUIRE(fixture.chain().publish_transition(database::transition_heights{
                .last_block_height = 102, .utxo_built_height = 102},
                blockchain::abla_records_for(1, *states)) == database::result_code::success);
            REQUIRE(fixture.chain().env_sync() == database::result_code::success);
            REQUIRE(fixture.restart());
            auto const stored = fixture.chain().get_utxo(chain::output_point{genesis.hash(), 0}, 103);
            REQUIRE(stored);
            REQUIRE(stored->output.token_data());
            CHECK(stored->output.token_data()->id == category);
            CHECK(chain::get_amount(*stored->output.token_data()) == 1);
            CHECK(stored->height == 102);
            auto spend = signed_spend(genesis,
                chain::output::list{token_output(category, amount, 999'000)}, settings.enabled_flags());
            REQUIRE_FALSE(spend.inputs()[0].previous_output().validation.cache.is_valid());
            batch.append({std::move(spend)}, 1000);
            REQUIRE(fixture.organizer().add_headers({batch.blocks.back()->header()}).headers_added == 1);
            blockchain::batch_rejection rejection;
            std::vector<block_const_ptr> const next{batch.blocks.back()};
            auto const ec = blockchain::validate_block_batch(fixture.chain(), settings,
                domain::config::network::regtest, next, 103, rejection);
            CHECK(ec == (amount == 1 ? error::success : error::token_fungible_insufficient));
            CHECK(rejection.block == (amount == 1 ? std::optional<size_t>{} : std::optional<size_t>{0}));
            auto const& read_back = next.front()->transactions()[1].inputs()[0].previous_output().validation;
            REQUIRE(read_back.cache.token_data());
            CHECK(chain::get_amount(*read_back.cache.token_data()) == 1);
            CHECK(read_back.height == 102);
        }
    }
}

TEST_CASE("batch contextual consensus: a stored PATFO remains unspendable after Descartes (BCHN: Upgrade 9)",
          "[batch_contextual_consensus][tokens][patfo]") {
    constexpr auto network = domain::config::network::scalenet;
    constexpr uint32_t activation = scalenet_descartes_activation_height;
    constexpr uint32_t candidate_height = activation + 1;
    for (uint32_t born : {activation - 1, activation}) {
        DYNAMIC_SECTION("token creation height=" << born) {
            chain_fixture fixture("batch_patfo", network);
            REQUIRE(fixture.created());
            fixture.settings().accept_non_standard_transactions = true;
            fixture.settings().checkpoints.clear();
            fixture.settings().derive_checkpoints();
            fixture.settings().notify_limit_hours = std::numeric_limits<uint32_t>::max();
            REQUIRE(fixture.start());
            auto& chain = fixture.chain();
            auto const& settings = fixture.settings();
            auto const cb = coinbase_tx(1, 0, 50 * satoshi_per_bitcoin);
            auto funding = signed_spend(cb,
                chain::output::list{token_output(cb.hash(), 1, 1'000'000)}, settings.enabled_flags());
            // Trusted historical headers isolate contextual block validation.
            // As in the ABLA activation fixture, this does not claim validation
            // of the historical headers' PoW or of the complete historical UTXO set.
            auto previous = chain::block::genesis_scalenet().hash();
            auto const first_time = chain::block::genesis_scalenet().header().timestamp();
            chain::header parent;
            std::optional<chain::block> funding_block;
            for (uint32_t h = 1; h < candidate_height; ++h) {
                parent = chain::header{0x20000000u, previous, null_hash,
                    first_time + h * 600u, 0x1d00ffffu, h};
                if (h == born) {
                    funding_block = mine_block(previous, h, first_time + h * 600u, 0, {funding}, 0);
                    parent = funding_block->header();
                }
                previous = chain::hash(parent);
                auto const added = chain.headers().add(previous, parent);
                REQUIRE(added.inserted);
                chain.headers().active_push(added.index);
            }
            REQUIRE_FALSE(chain.replace_headers_from({parent}, candidate_height - 1));
            chain::abla::state const parent_state(settings.abla_config, 1000, 600);
            std::vector<database::header_abla_record> records{{candidate_height - 1,
                parent_state.block_size, parent_state.control_block_size, parent_state.elastic_buffer_size}};
            REQUIRE(chain.publish_transition(database::transition_heights{
                .last_block_height = candidate_height - 1, .utxo_built_height = candidate_height - 1}, records)
                == database::result_code::success);
            REQUIRE(funding_block);
            store_outputs(fixture, {std::make_shared<domain::message::block const>(*funding_block)},
                born, network, false);
            auto spend = signed_spend(funding,
                chain::output::list{token_output(cb.hash(), 1, 999'000)}, settings.enabled_flags());
            auto candidate = std::make_shared<domain::message::block const>(mine_block(previous,
                candidate_height, first_time + candidate_height * 600u, 0, {std::move(spend)}, 1000));
            REQUIRE(chain.headers().add(candidate->hash(), candidate->header()).inserted);
            blockchain::batch_rejection rejection;
            auto const ec = blockchain::validate_block_batch(chain, settings, network,
                {candidate}, candidate_height, rejection);
            CHECK(ec == (born < activation ? error::token_input_created_pre_activation : error::success));
            CHECK(rejection.block == (born < activation ? std::optional<size_t>{0} : std::optional<size_t>{}));

            REQUIRE(chain.begin_transition());
            auto const published = chain.publish_chain_view(candidate_height - 1);
            chain.end_transition();
            REQUIRE_FALSE(published);
            auto tx = std::make_shared<domain::message::transaction const>(candidate->transactions()[1]);
            auto channel = chain.subscribe_transaction();
            auto admitted = ::asio::co_spawn(chain.executor(),
                [&]() -> ::asio::awaitable<code> { co_return co_await chain.submit_transaction(tx); },
                ::asio::use_future).get();
            CHECK(admitted == (born < activation ? error::token_input_created_pre_activation : error::success));
            CHECK(chain.mempool_ref().contains(tx->hash()) == (born >= activation));
            bool announced = false;
            while (channel->try_receive([&](std::error_code, transaction_const_ptr item) {
                announced = announced || item->hash() == tx->hash();
            })) {}
            CHECK(announced == (born >= activation));
            chain.unsubscribe_transaction(channel);
        }
    }
}
TEST_CASE("batch contextual consensus: a token-free input cannot forge an unrelated category",
          "[batch_contextual_consensus][tokens]") {
    blockchain::settings settings(domain::config::network::regtest);
    batch_chain batch;
    REQUIRE(batch.coinbase_one().hash() != null_hash);
    auto spend = signed_spend(batch.coinbase_one(),
        chain::output::list{token_output(null_hash, 1, 1'000'000)}, settings.enabled_flags());
    batch.append({std::move(spend)}, batch.coinbase_one().outputs()[0].value() - 1'000'000);
    auto const answer = validate("batch_invalid_category", batch, settings);
    CHECK(answer.result == error::token_invalid_category);
    CHECK(answer.rejection.block == std::optional<size_t>{101});
}
TEST_CASE("batch contextual consensus: malformed token output prefixes depend on Descartes (BCHN: Upgrade 9)",
          "[batch_contextual_consensus][tokens][token_prefix]") {
    data_chunk long_prefix(35, 0);
    long_prefix[0] = 0xef;
    long_prefix[33] = 0x80;
    for (bool active : {false, true}) {
        for (data_chunk bytes : {data_chunk{0xef}, data_chunk(33, 0xef), long_prefix, data_chunk{0x6a, 0xef}}) {
            DYNAMIC_SECTION("active=" << active << ", script=" << encode_base16(bytes)) {
                blockchain::settings settings(domain::config::network::regtest);
                settings.bch_descartes = active;
                settings.bch_lobachevski = active;
                settings.bch_galois = active;
                settings.bch_leibniz = active;
                batch_chain batch;
                auto tx = signed_spend(batch.coinbase_one(),
                    chain::output::list{chain::output(1'000'000, chain::script(bytes, false), chain::token_data_opt{})},
                    settings.enabled_flags());
                batch.append({std::move(tx)}, batch.coinbase_one().outputs()[0].value() - 1'000'000);
                auto const raw = to_data_chunk(static_cast<chain::block const&>(*batch.blocks.back()));
                byte_reader reader(raw);
                auto parsed = domain::message::block::from_data(reader, 0);
                REQUIRE(parsed);
                batch.blocks.back() = std::make_shared<domain::message::block const>(std::move(*parsed));
                auto const answer = validate("batch_token_prefix", batch, settings);
                bool const bad = active && bytes.front() == 0xef;
                CHECK(answer.result == (bad ? error::token_unparseable_output : error::success));
                CHECK(answer.rejection.block == (bad ? std::optional<size_t>{101} : std::optional<size_t>{}));
            }
        }
    }
}
TEST_CASE("batch contextual consensus: coinbase malformed token prefixes are rejected",
          "[batch_contextual_consensus][tokens][token_prefix][coinbase]") {
    blockchain::settings settings(domain::config::network::regtest);
    batch_chain batch;
    batch.replace_tip_coinbase(2, {}, chain::script(data_chunk{0xef}, false));
    auto const answer = validate("batch_cb_token_prefix", batch, settings);
    CHECK(answer.result == error::token_unparseable_output);
    CHECK(answer.rejection.block == std::optional<size_t>{100});
}
TEST_CASE("batch contextual consensus: noncanonical token payloads are not parseable tokens",
          "[batch_contextual_consensus][tokens][token_prefix][token_canonical]") {
    for (bool active : {false, true}) {
        for (data_chunk suffix : {data_chunk{0x30, 0}, data_chunk{0x60, 0},
                data_chunk{0x10, 0xff, 0, 0, 0, 0, 0, 0, 0, 0x80}}) {
            DYNAMIC_SECTION("active=" << active << ", suffix=" << encode_base16(suffix)) {
                blockchain::settings settings(domain::config::network::regtest);
                settings.bch_descartes = active;
                settings.bch_lobachevski = active;
                settings.bch_galois = active;
                settings.bch_leibniz = active;
                batch_chain batch;
                auto const category = batch.coinbase_one().hash();
                data_chunk raw_script{0xef};
                raw_script.insert(raw_script.end(), category.begin(), category.end());
                raw_script.insert(raw_script.end(), suffix.begin(), suffix.end());
                auto const& locking = miner_script().bytes();
                raw_script.insert(raw_script.end(), locking.begin(), locking.end());
                auto tx = signed_spend(batch.coinbase_one(),
                    chain::output::list{chain::output(1'000'000, chain::script(raw_script, false), chain::token_data_opt{})},
                    settings.enabled_flags());
                batch.append({std::move(tx)}, batch.coinbase_one().outputs()[0].value() - 1'000'000);
                auto const raw = to_data_chunk(static_cast<chain::block const&>(*batch.blocks.back()));
                byte_reader reader(raw);
                auto parsed = domain::message::block::from_data(reader, 0);
                REQUIRE(parsed);
                batch.blocks.back() = std::make_shared<domain::message::block const>(std::move(*parsed));
                auto const answer = validate("batch_noncanonical_token", batch, settings);
                CHECK(answer.result == (active ? error::token_unparseable_output : error::success));
                CHECK(answer.rejection.block == (active ? std::optional<size_t>{101} : std::optional<size_t>{}));
            }
        }
    }
}
#endif // KTH_CURRENCY_BCH
