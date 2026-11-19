// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers, against peers in this process: each
// listens on a port the OS assigns, serves a regtest chain with its script
// and input prefix filters, and answers what a Knuth node answers.

#include "wallet_peer_harness.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/node/wallet/blocks.hpp>

#include "../../blockchain/test/reorg_chain_fixture.hpp"

TEST_CASE("wallet from peers: histories, balances, a transaction by hash alone, RPA, and nothing watched sent", "[wallet][wallet_peers]") {
    answers_chain made;
    auto& served = made.served;
    auto const& category = made.category;
    auto const& nft = made.nft;
    auto const& paying = made.paying;
    auto const& spend = made.spend;
    auto const& again = made.again;
    auto const& unrelated = made.unrelated;
    auto const& ground = made.ground;

    test_peer peer(served);
    temporary_directory dir("answers");
    auto settings = settings_for(dir.path, {peer.endpoint()});
    settings.sent_log = dir.path / "sent.log";
    running_service running(settings);
    auto& service = *running.service;

    auto const synced = wait_for(service, service.wait_synced(30s));
    REQUIRE(synced);
    REQUIRE(synced->header_height == 30u);
    REQUIRE(synced->tip == 30u);
    // RPA downloads whole ranges: no input prefix filters are synced.
    REQUIRE( ! synced->input_prefix_tip);
    REQUIRE(synced->transaction_keys == true);

    auto const target = wallet::target::of_script(watched_script);
    auto const history = wait_for(service, service.history(target));
    REQUIRE(history);
    REQUIRE(history->synced == 30u);
    REQUIRE(history->confirmed.size() == 3u);
    REQUIRE(history->confirmed[0].txid == paying.hash());
    REQUIRE(history->confirmed[0].height == 5u);
    REQUIRE(history->confirmed[1].txid == spend.hash());
    REQUIRE(history->confirmed[1].inputs.at(0).value == 1000u);
    REQUIRE(history->confirmed[2].txid == again.hash());
    REQUIRE(history->unconfirmed.empty());

    auto const balance = wait_for(service, service.get_balance(target, wallet::token_filter::include));
    REQUIRE(balance);
    REQUIRE(balance->confirmed == 500u);
    REQUIRE(balance->unconfirmed == 0);
    auto const coins = wait_for(service, service.unspent(target, wallet::token_filter::include));
    REQUIRE(coins);
    REQUIRE(coins->size() == 1u);
    REQUIRE(coins->at(0).outpoint == chain::point(again.hash(), 0));
    REQUIRE(coins->at(0).height == 12u);

    // A token category, followed to the script it was sent to.
    auto const tokens = wait_for(service, service.unspent(wallet::target::of_category(category), wallet::token_filter::only));
    REQUIRE(tokens);
    REQUIRE(tokens->size() == 1u);
    REQUIRE(tokens->at(0).token == chain::token_data_opt{nft});
    REQUIRE(tokens->at(0).script == other_script);

    // A transaction no watched target touches, by its hash alone.
    auto const found = wait_for(service, service.transaction(unrelated.hash(), std::nullopt));
    REQUIRE(found);
    REQUIRE(found->tx.hash() == unrelated.hash());
    REQUIRE(found->confirmed);
    REQUIRE(found->confirmed->height == 15u);
    auto const proof = wait_for(service, service.merkle(unrelated.hash(), std::nullopt));
    REQUIRE(proof);
    REQUIRE(chain::block::merkle_branch_root(unrelated.hash(), proof->confirmed.position, proof->branch) ==
        served.blocks[15].header().merkle());
    auto const missing = wait_for(service, service.transaction(digest_of(999), std::nullopt));
    REQUIRE( ! missing);
    REQUIRE(missing.error() == error::not_found);

    // RPA: every transaction with a counted input in the prefix.
    auto const value = chain::input_prefix(ground.inputs()[0]);
    auto const matches = wait_for(service, service.rpa_history(value, 16, 0, std::nullopt));
    REQUIRE(matches);
    auto const ground_match = std::ranges::find_if(*matches, [&](auto const& match) { return match.txid == ground.hash(); });
    REQUIRE(ground_match != matches->end());
    REQUIRE(ground_match->height == 20u);
    REQUIRE(std::ranges::find(ground_match->inputs, 0u) != ground_match->inputs.end());
    auto const byte_prefix = wait_for(service, service.rpa_history(uint16_t(value >> 8), 8, 0, std::nullopt));
    REQUIRE(byte_prefix);
    REQUIRE(std::ranges::any_of(*byte_prefix, [&](auto const& match) { return match.txid == ground.hash(); }));
    REQUIRE( ! wait_for(service, service.rpa_history(0x1ff, 8, 0, std::nullopt)));

    // An output by its outpoint: unspent, and spent by a later block.
    auto const kept = wait_for(service, service.output(chain::point(again.hash(), 0)));
    REQUIRE(kept);
    REQUIRE(*kept);
    REQUIRE((*kept)->height == 12u);
    REQUIRE((*kept)->value == 500u);
    auto const gone = wait_for(service, service.output(chain::point(paying.hash(), 0)));
    REQUIRE(gone);
    REQUIRE( ! *gone);
    auto const none = wait_for(service, service.output(chain::point(paying.hash(), 9)));
    REQUIRE(none);
    REQUIRE( ! *none);

    // Header proofs: each branch folds its block hash to the same root.
    auto const branch5 = wait_for(service, service.header_branch(5, 10));
    auto const branch9 = wait_for(service, service.header_branch(9, 10));
    REQUIRE(branch5);
    REQUIRE(branch9);
    REQUIRE(branch5->second == branch9->second);
    REQUIRE(chain::block::merkle_branch_root(served.blocks[5].hash(), 5, branch5->first) == branch5->second);
    REQUIRE(chain::block::merkle_branch_root(served.blocks[9].hash(), 9, branch9->first) == branch9->second);
    REQUIRE( ! wait_for(service, service.header_branch(11, 10)));
    REQUIRE(wait_for(service, service.dsproof_transactions()).empty());
    REQUIRE( ! wait_for(service, service.dsproof(again.hash())));
    REQUIRE(wait_for(service, service.changed(0, 0s)) > 0u);

    // Nothing sent names what was watched or looked up.
    std::ifstream log(settings.sent_log);
    std::string line;
    std::vector<std::string> secrets{
        encode_base16(watched_script),
        encode_base16(chain::script_key(watched_script)),
        encode_hash(chain::script_key(watched_script)),
        encode_base16(chain::category_key(category)),
        encode_base16(chain::transaction_key(unrelated.hash())),
        encode_base16(unrelated.hash()),
        encode_hash(unrelated.hash()),
    };
    size_t lines = 0;
    while (std::getline(log, line)) {
        ++lines;
        auto const command = line.substr(0, line.find(' '));
        REQUIRE(command != "getcfrange");
        REQUIRE(command != "getcfproof");
        REQUIRE(command != "filterload");
        REQUIRE(command != "filteradd");
        for (auto const& secret : secrets) REQUIRE(line.find(secret) == std::string::npos);
    }
    REQUIRE(lines > 0u);

    // A broadcast, over a connection of its own: no reject after its pong.
    auto const fresh = spending({chain::point(again.hash(), 0)}, {{400, other_script}}, 6);
    auto const sessions = peer.sessions.load();
    auto const sent = wait_for(service, service.broadcast(fresh));
    REQUIRE(sent);
    REQUIRE(*sent == fresh.hash());
    REQUIRE(peer.sessions.load() == sessions + 1);
    auto const pending = wait_for(service, service.history(target));
    REQUIRE(pending);
    REQUIRE(pending->unconfirmed.size() == 1u);
    REQUIRE(pending->unconfirmed[0].inputs.at(0).value == 500u);
    auto const after = wait_for(service, service.get_balance(target, wallet::token_filter::include));
    REQUIRE(after);
    REQUIRE(after->unconfirmed == -500);
}

TEST_CASE("wallet from peers: a filter that does not fit its block is found and dropped", "[wallet][wallet_peers]") {
    served_chain served;
    for (uint32_t height = 1; height <= 10; ++height) {
        served.mine({spending({chain::point(digest_of(200 + height), 0)}, {{10, other_script}}, height)});
    }
    // The tip's filter leaves out the block's outputs, which the client
    // finds when it downloads the tip.
    auto const tip = served.blocks.back().hash();
    std::vector<data_chunk> wrong{to_chunk(digest_of(5))};
    auto const set = chain::golomb_set::build(chain::compact_filter_key(tip), wrong);
    REQUIRE(set);
    data_chunk served_bytes(hash_size, 0);
    auto const bytes = set->to_data();
    served_bytes.insert(served_bytes.end(), bytes.begin(), bytes.end());
    served.replace_script_filter(10, served_bytes);

    test_peer peer(served);
    temporary_directory dir("dispute");
    running_service running(settings_for(dir.path, {peer.endpoint()}));
    auto& service = *running.service;
    // The peer that served it is refused, which stays in the status.
    REQUIRE(status_reaches(service, [](wallet::status const& now) {
        return std::ranges::any_of(now.refused, [](std::string const& why) {
            return why.find("does not fit block 10") != std::string::npos;
        });
    }));
    REQUIRE(service.current().tip.value_or(0) < 10u);
}

TEST_CASE("wallet from peers: a birthday syncs only the filters after it", "[wallet][wallet_peers]") {
    served_chain served;
    auto const early = spending({chain::point(digest_of(300), 0)}, {{100, watched_script}}, 1);
    auto const late = spending({chain::point(digest_of(301), 0)}, {{200, watched_script}}, 2);
    for (uint32_t height = 1; height <= 1010; ++height) {
        std::vector<chain::transaction> txs;
        if (height == 3) txs.push_back(early);
        if (height == 1004) txs.push_back(late);
        served.mine(std::move(txs));
    }

    SECTION("birthday") {
        test_peer peer(served);
        temporary_directory dir("birthday");
        auto settings = settings_for(dir.path, {peer.endpoint()});
        settings.birthday = 1000;
        running_service running(settings);
        auto& service = *running.service;
        auto const synced = wait_for(service, service.wait_synced(60s));
        REQUIRE(synced);
        REQUIRE(synced->tip == 1010u);
        // Eleven filters, not a thousand and eleven.
        REQUIRE(synced->filter_bytes < 11u * 200u);
        auto const history = wait_for(service, service.history(wallet::target::of_script(watched_script)));
        REQUIRE(history);
        REQUIRE(history->confirmed.size() == 1u);
        REQUIRE(history->confirmed[0].txid == late.hash());
    }
}

TEST_CASE("wallet from the node's own chain answers as from peers", "[wallet][wallet_chain]") {
    // Blocks 1 to 110 pay the miner's script; block 101 spends block 1's
    // coinbase back to it, so that coinbase is fully spent.
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
    auto const base_time = uint32_t(zulu_time()) - 140 * test::block_spacing;
    std::vector<chain::block> blocks;
    auto previous = chain::block::genesis_regtest().hash();
    std::optional<chain::transaction> spend;
    for (uint32_t height = 1; height <= 110; ++height) {
        std::vector<chain::transaction> txs;
        uint64_t fees = 0;
        if (height == 101) {
            auto const& coinbase = blocks.front().transactions().front();
            spend = test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000, flags);
            txs.push_back(*spend);
            fees = 1000;
        }
        blocks.push_back(test::mine_block(previous, height, base_time + height * test::block_spacing, 3, std::move(txs), fees));
        previous = blocks.back().hash();
    }
    test::chain_fixture fixture("wallet_chain_source");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());
    test::persist_headers(fixture, blocks, 1);
    test::connect_bodies(fixture, blocks, 1);
    REQUIRE(fixture.chain().chain_view()->connected_tip_height == 110u);

    auto own = wallet::service::from_chain(fixture.chain(), domain::config::network::regtest);
    REQUIRE(own->current().source == "chain");

    // Found by its hash alone, before anything is watched: a coinbase whose
    // only output is spent.
    auto const coinbase_txid = blocks.front().transactions().front().hash();
    auto const found = wait_for(*own, own->transaction(coinbase_txid, std::nullopt));
    REQUIRE(found);
    REQUIRE(found->confirmed);
    REQUIRE(found->confirmed->height == 1u);
    REQUIRE( ! wait_for(*own, own->transaction(digest_of(4242), std::nullopt)));

    auto const miner = wallet::target::of_script(test::miner_script().bytes());
    auto const own_history = wait_for(*own, own->history(miner));
    REQUIRE(own_history);
    REQUIRE(own_history->synced == 110u);
    REQUIRE(own_history->confirmed.size() == 111u);
    auto const own_balance = wait_for(*own, own->get_balance(miner, wallet::token_filter::include));
    REQUIRE(own_balance);
    auto const own_coins = wait_for(*own, own->unspent(miner, wallet::token_filter::include));
    REQUIRE(own_coins);
    REQUIRE(own_coins->size() == 110u);
    auto const value = chain::input_prefix(spend->inputs()[0]);
    auto const own_rpa = wait_for(*own, own->rpa_history(value, 16, 0, std::nullopt));
    REQUIRE(own_rpa);
    REQUIRE(std::ranges::any_of(*own_rpa, [&](auto const& match) { return match.txid == spend->hash() && match.height == 101u; }));

    // The same blocks, served by a peer: the answers are the same.
    served_chain served;
    for (auto const& blk : blocks) served.append(blk);
    test_peer peer(served);
    temporary_directory dir("chain_equality");
    running_service running(settings_for(dir.path, {peer.endpoint()}));
    auto& from_peers = *running.service;
    REQUIRE(wait_for(from_peers, from_peers.wait_synced(30s)));
    auto const peer_history = wait_for(from_peers, from_peers.history(miner));
    REQUIRE(peer_history);
    auto const entries = [](wallet::target_history const& history) {
        std::vector<std::pair<hash_digest, uint32_t>> out;
        for (auto const& tx : history.confirmed) out.emplace_back(tx.txid, tx.height);
        return out;
    };
    REQUIRE(entries(*peer_history) == entries(*own_history));
    auto const peer_balance = wait_for(from_peers, from_peers.get_balance(miner, wallet::token_filter::include));
    REQUIRE(peer_balance);
    REQUIRE(peer_balance->confirmed == own_balance->confirmed);
    auto const peer_coins = wait_for(from_peers, from_peers.unspent(miner, wallet::token_filter::include));
    REQUIRE(peer_coins);
    auto const outpoints = [](std::vector<wallet::utxo> const& coins) {
        std::vector<chain::point> out;
        for (auto const& coin : coins) out.push_back(coin.outpoint);
        std::ranges::sort(out);
        return out;
    };
    REQUIRE(outpoints(*peer_coins) == outpoints(*own_coins));
    auto const peer_rpa = wait_for(from_peers, from_peers.rpa_history(value, 16, 0, std::nullopt));
    REQUIRE(peer_rpa);
    auto const matches = [](std::vector<wallet::rpa_match> const& listed) {
        std::vector<std::pair<hash_digest, uint32_t>> out;
        for (auto const& match : listed) out.emplace_back(match.txid, match.height.value_or(0));
        std::ranges::sort(out);
        return out;
    };
    REQUIRE(matches(*peer_rpa) == matches(*own_rpa));
    auto const peer_found = wait_for(from_peers, from_peers.transaction(coinbase_txid, std::nullopt));
    REQUIRE(peer_found);
    REQUIRE(peer_found->confirmed->height == 1u);
}

// Expected to fail until the dsproof message parses: its spender reads its
// push data with read_remaining_bytes(), so the first spender takes the
// second's bytes and no relayed proof parses (k-nuth/kth#589, #587; the
// specification's push data is a counted list of pushes). The test peer
// serves the proof and the client receives it; it then fails to parse.
TEST_CASE("wallet double-spend proofs: the full node lists its own as the peer path lists those relayed", "[wallet][wallet_chain][!shouldfail]") {
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
    auto const base_time = uint32_t(zulu_time()) - 140 * test::block_spacing;
    std::vector<chain::block> blocks;
    auto previous = chain::block::genesis_regtest().hash();
    for (uint32_t height = 1; height <= 110; ++height) {
        blocks.push_back(test::mine_block(previous, height, base_time + height * test::block_spacing, 9, {}, 0));
        previous = blocks.back().hash();
    }
    test::chain_fixture fixture("wallet_dsproofs");
    REQUIRE(fixture.created());
    REQUIRE(fixture.start());
    REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());
    test::persist_headers(fixture, blocks, 1);
    test::connect_bodies(fixture, blocks, 1);
    auto& node_chain = fixture.chain();

    // A mempool transaction spending block 1's coinbase, and a proof that the
    // same output was spent twice.
    auto const& coinbase = blocks.front().transactions().front();
    auto const pending = test::spend_p2pkh(coinbase, 0, coinbase.outputs()[0].value() - 1000, flags);
    auto const pooled = std::make_shared<message::transaction const>(pending);
    auto submitted = ::asio::co_spawn(node_chain.executor(), [&node_chain, pooled]() -> ::asio::awaitable<code> {
        co_return co_await node_chain.submit_transaction(pooled);
    }, ::asio::use_future);
    REQUIRE(submitted.get() == error::success);
    message::double_spend_proof::spender first{};
    first.out_sequence = 1;
    first.push_data = data_chunk{0x01, 0x02};
    message::double_spend_proof::spender second{};
    second.out_sequence = 2;
    second.push_data = data_chunk{0x03, 0x04};
    auto const proof = std::make_shared<message::double_spend_proof const>(chain::output_point{coinbase.hash(), 0}, first, second);
    auto organized = ::asio::co_spawn(node_chain.executor(), [&node_chain, proof]() -> ::asio::awaitable<code> {
        co_return co_await node_chain.organize(proof);
    }, ::asio::use_future);
    REQUIRE(organized.get() == error::success);

    auto own = wallet::service::from_chain(node_chain, domain::config::network::regtest);
    auto const own_list = wait_for(*own, own->dsproof_transactions());
    REQUIRE(own_list == std::vector<hash_digest>{pending.hash()});
    auto const own_proof = wait_for(*own, own->dsproof(pending.hash()));
    REQUIRE(own_proof);
    REQUIRE(own_proof->dspid == proof->hash());
    REQUIRE(own_proof->txid == pending.hash());
    auto const by_hash = wait_for(*own, own->dsproof(proof->hash()));
    REQUIRE(by_hash);

    // The same chain, mempool transaction and proof, relayed by a peer.
    served_chain served;
    for (auto const& blk : blocks) served.append(blk);
    served.pooled.push_back(pending);
    served.proofs.push_back(*proof);
    test_peer peer(served);
    temporary_directory dir("dsproofs");
    running_service running(settings_for(dir.path, {peer.endpoint()}));
    auto& from_peers = *running.service;
    REQUIRE(wait_for(from_peers, from_peers.wait_synced(30s)));
    // A relayed proof is a change: woken by it, not by sleeping between looks.
    auto seen = wait_for(from_peers, from_peers.changed(0, 0s));
    auto peer_list = wait_for(from_peers, from_peers.dsproof_transactions());
    auto const deadline = std::chrono::steady_clock::now() + 30s;
    while (peer_list.empty() && std::chrono::steady_clock::now() < deadline) {
        seen = wait_for(from_peers, from_peers.changed(seen, 5s));
        peer_list = wait_for(from_peers, from_peers.dsproof_transactions());
    }
    auto const copied = wait_for(from_peers, from_peers.mempool());
    auto const proof_known = wait_for(from_peers, from_peers.dsproof(proof->hash()));
    INFO("sessions " << peer.sessions.load() << ", announcements " << peer.announcements.load()
        << ", transactions served " << peer.served_transactions.load() << ", proofs served " << peer.served_proofs.load()
        << ", mempool copy " << copied.count << ", proof known " << bool(proof_known));
    REQUIRE(peer_list == own_list);
    auto const peer_proof = wait_for(from_peers, from_peers.dsproof(pending.hash()));
    REQUIRE(peer_proof);
    REQUIRE(peer_proof->dspid == own_proof->dspid);
    REQUIRE(peer_proof->raw == own_proof->raw);
    REQUIRE(peer_proof->outpoint == own_proof->outpoint);
}

namespace {

size_t getdata_lines(std::filesystem::path const& log_path) {
    std::ifstream log(log_path);
    std::string line;
    size_t count = 0;
    while (std::getline(log, line)) count += line.starts_with("getdata ") ? 1 : 0;
    return count;
}

std::vector<hash_digest> confirmed_txids(wallet::target_history const& history) {
    std::vector<hash_digest> out;
    for (auto const& entry : history.confirmed) out.push_back(entry.txid);
    return out;
}

bool refused_with(wallet::status const& now, std::string const& endpoint, std::string const& why) {
    return std::ranges::any_of(now.refused, [&](std::string const& entry) {
        return entry.starts_with(endpoint) && entry.find(why) != std::string::npos;
    });
}

} // namespace

TEST_CASE("wallet from peers: peers that misstate filter checkpoints or headers are refused and the others serve", "[wallet][wallet_peers]") {
    served_chain served;
    auto const early = spending({chain::point(digest_of(310), 0)}, {{100, watched_script}}, 1);
    auto const late = spending({chain::point(digest_of(311), 0)}, {{200, watched_script}}, 2);
    for (uint32_t height = 1; height <= 1010; ++height) {
        std::vector<chain::transaction> txs;
        if (height == 3) txs.push_back(early);
        if (height == 1004) txs.push_back(late);
        served.mine(std::move(txs));
    }
    auto const settled = [&](wallet::service& service) {
        auto const synced = wait_for(service, service.wait_synced(60s));
        REQUIRE(synced);
        REQUIRE(synced->tip == 1010u);
        auto const history = wait_for(service, service.history(wallet::target::of_script(watched_script)));
        REQUIRE(history);
        REQUIRE(confirmed_txids(*history) == std::vector<hash_digest>{early.hash(), late.hash()});
    };

    SECTION("a checkpoint list one short") {
        auto other = served;
        other.short_checkpoints = true;
        test_peer liar(other);
        test_peer honest(served);
        temporary_directory dir("short_checkpoints");
        running_service running(settings_for(dir.path, {liar.endpoint(), honest.endpoint()}));
        auto& service = *running.service;
        REQUIRE(status_reaches(service, [&](wallet::status const& now) {
            return refused_with(now, liar.endpoint(), "filter checkpoints");
        }));
        settled(service);
    }

    SECTION("checkpoints that differ, settled by the block where the filter headers part") {
        auto other = served;
        other.replace_script_filter(500, data_chunk(40, 0x33));
        test_peer liar(other);
        test_peer honest(served);
        temporary_directory dir("disagree");
        running_service running(settings_for(dir.path, {liar.endpoint(), honest.endpoint()}));
        auto& service = *running.service;
        REQUIRE(status_reaches(service, [&](wallet::status const& now) {
            return refused_with(now, liar.endpoint(), "does not fit block 500");
        }));
        settled(service);
        REQUIRE( ! refused_with(service.current(), honest.endpoint(), ""));
    }
}

TEST_CASE("wallet from peers: a filter server a block behind the headers is asked again once it catches up", "[wallet][wallet_peers]") {
    // The filters' peer stays silent for a tip it does not have (BIP157):
    // that is no sign it serves no filters.
    served_chain behind;
    for (uint32_t height = 1; height <= 20; ++height) behind.mine({});
    auto ahead = behind;
    ahead.mine({});
    test_peer filters(behind);
    test_peer headers(ahead, false);
    temporary_directory dir("lagging_filters");
    running_service running(settings_for(dir.path, {filters.endpoint(), headers.endpoint()}));
    auto& service = *running.service;
    REQUIRE(status_reaches(service, [](wallet::status const& now) { return now.header_height == 21u; }));
    // Asked for block 21's checkpoints, it stays silent; the request times
    // out, and the round ends without them.
    REQUIRE(filters.silent.get_future().wait_for(30s) == std::future_status::ready);
    filters.switch_to(ahead);
    REQUIRE(status_reaches(service, [](wallet::status const& now) { return now.synced && now.tip == 21u; }));
}

TEST_CASE("wallet from peers: a filter above the last checkpoint is compared with another peer's and settled by its block", "[wallet][wallet_peers]") {
    served_chain served;
    auto const paid = spending({chain::point(digest_of(320), 0)}, {{700, watched_script}}, 1);
    for (uint32_t height = 1; height <= 30; ++height) {
        std::vector<chain::transaction> txs;
        if (height == 25) txs.push_back(paid);
        served.mine(std::move(txs));
    }
    // The liar's filter of block 25 leaves the payment out: a wallet that
    // took it would never see it.
    auto other = served;
    auto const set = chain::golomb_set::build(chain::compact_filter_key(served.blocks[25].hash()),
        std::vector<data_chunk>{to_chunk(digest_of(321))});
    REQUIRE(set);
    data_chunk lie(hash_size, 0);
    auto const bytes = set->to_data();
    lie.insert(lie.end(), bytes.begin(), bytes.end());
    other.replace_script_filter(25, lie);

    // The liar is asked first.
    test_peer liar(other);
    test_peer honest(served);
    temporary_directory dir("cross_check");
    running_service running(settings_for(dir.path, {liar.endpoint(), honest.endpoint()}));
    auto& service = *running.service;
    REQUIRE(status_reaches(service, [&](wallet::status const& now) {
        return refused_with(now, liar.endpoint(), "does not fit block 25");
    }));
    REQUIRE(status_reaches(service, [](wallet::status const& now) { return now.synced && now.tip == 30u; }));
    auto const history = wait_for(service, service.history(wallet::target::of_script(watched_script)));
    REQUIRE(history);
    REQUIRE(confirmed_txids(*history) == std::vector<hash_digest>{paid.hash()});
}

TEST_CASE("wallet from peers: a reorganization rolls the histories back, below the birthday too", "[wallet][wallet_peers]") {
    served_chain first;
    auto const orphaned = spending({chain::point(digest_of(330), 0)}, {{800, watched_script}}, 1);
    for (uint32_t height = 1; height <= 40; ++height) {
        std::vector<chain::transaction> txs;
        if (height == 35) txs.push_back(orphaned);
        first.mine(std::move(txs));
    }
    // A heavier branch from `fork`, paying the watched script at 38 instead.
    auto const branch = [&](uint32_t fork) {
        served_chain second;
        second.base_time = first.base_time;
        for (uint32_t height = 1; height <= fork; ++height) second.append(first.blocks[height]);
        return second;
    };
    auto const replacing = spending({chain::point(digest_of(331), 0)}, {{900, watched_script}}, 2);

    SECTION("above the birthday") {
        auto second = branch(30);
        for (uint32_t height = 31; height <= 45; ++height) {
            std::vector<chain::transaction> txs;
            if (height == 38) txs.push_back(replacing);
            second.mine(std::move(txs), 1000 + height);
        }
        test_peer peer(first);
        temporary_directory dir("peer_reorg");
        running_service running(settings_for(dir.path, {peer.endpoint()}));
        auto& service = *running.service;
        REQUIRE(wait_for(service, service.wait_synced(30s)));
        auto const target = wallet::target::of_script(watched_script);
        auto const before = wait_for(service, service.history(target));
        REQUIRE(before);
        REQUIRE(confirmed_txids(*before) == std::vector<hash_digest>{orphaned.hash()});
        peer.switch_to(second);
        REQUIRE(status_reaches(service, [](wallet::status const& now) { return now.synced && now.tip == 45u; }));
        auto const after = wait_for(service, service.history(target));
        REQUIRE(after);
        REQUIRE(confirmed_txids(*after) == std::vector<hash_digest>{replacing.hash()});
        REQUIRE( ! wait_for(service, service.transaction(orphaned.hash(), std::nullopt)));
    }

    SECTION("below the birthday") {
        // The store starts at 32; the fork at 25 empties it.
        auto second = branch(25);
        for (uint32_t height = 26; height <= 45; ++height) {
            std::vector<chain::transaction> txs;
            if (height == 38) txs.push_back(replacing);
            second.mine(std::move(txs), 2000 + height);
        }
        test_peer peer(first);
        temporary_directory dir("peer_reorg_birthday");
        auto settings = settings_for(dir.path, {peer.endpoint()});
        settings.birthday = 32;
        running_service running(settings);
        auto& service = *running.service;
        REQUIRE(wait_for(service, service.wait_synced(30s)));
        peer.switch_to(second);
        REQUIRE(status_reaches(service, [](wallet::status const& now) { return now.synced && now.tip == 45u; }));
        auto const after = wait_for(service, service.history(wallet::target::of_script(watched_script)));
        REQUIRE(after);
        REQUIRE(confirmed_txids(*after) == std::vector<hash_digest>{replacing.hash()});
    }
}

TEST_CASE("wallet from peers: a transaction a peer pushes without being asked is not taken", "[wallet][wallet_peers]") {
    served_chain served;
    for (uint32_t height = 1; height <= 10; ++height) served.mine({});
    auto const announced = spending({chain::point(digest_of(340), 0)}, {{50, watched_script}}, 1);
    auto const pushed = spending({chain::point(digest_of(341), 0)}, {{5000, watched_script}}, 2);
    served.pooled.push_back(announced);
    served.pushed.push_back(pushed);
    test_peer peer(served);
    temporary_directory dir("pushed");
    running_service running(settings_for(dir.path, {peer.endpoint()}));
    auto& service = *running.service;
    REQUIRE(wait_for(service, service.wait_synced(30s)));
    auto const target = wallet::target::of_script(watched_script);
    // The pushed transaction came first; once the announced one is taken,
    // the pushed one was seen and dropped.
    auto seen = wait_for(service, service.changed(0, 0s));
    auto const deadline = std::chrono::steady_clock::now() + 30s;
    std::optional<wallet::target_history> history;
    while (std::chrono::steady_clock::now() < deadline) {
        auto const now = wait_for(service, service.history(target));
        REQUIRE(now);
        history = *now;
        if ( ! history->unconfirmed.empty()) break;
        seen = wait_for(service, service.changed(seen, 5s));
    }
    REQUIRE(history);
    REQUIRE(history->unconfirmed.size() == 1u);
    REQUIRE(history->unconfirmed[0].txid == announced.hash());
    REQUIRE( ! wait_for(service, service.transaction(pushed.hash(), std::nullopt)));
}

TEST_CASE("wallet from peers: which blocks are downloaded names nothing watched, and what that costs", "[wallet][wallet_peers]") {
    // Two whole buckets of 144 blocks and part of a third.
    served_chain served;
    auto const first_paid = spending({chain::point(digest_of(350), 0)}, {{100, watched_script}}, 1);
    auto const second_paid = spending({chain::point(digest_of(351), 0)}, {{200, watched_script}}, 2);
    auto const ground = spending({chain::point(digest_of(352), 0), chain::point(digest_of(353), 0)}, {{66, other_script}}, 3);
    for (uint32_t height = 1; height <= 300; ++height) {
        std::vector<chain::transaction> txs;
        if (height == 50) txs.push_back(first_paid);
        if (height == 200) txs.push_back(second_paid);
        if (height == 100) txs.push_back(ground);
        if (height % 7 == 0) txs.push_back(spending({chain::point(digest_of(1000 + height), 0)}, {{1, other_script}}, height));
        served.mine(std::move(txs));
    }
    test_peer peer(served);
    temporary_directory dir("privacy");
    auto const log_path = dir.path / "sent.log";
    auto settings = settings_for(dir.path, {peer.endpoint()});
    settings.sent_log = log_path;
    auto const target = wallet::target::of_script(watched_script);
    auto const bucket = [](uint32_t height) { return height / 144; };

    std::set<uint32_t> first_session;
    uint64_t covered_bytes = 0;
    uint64_t sessions_before = 0;
    uint64_t sessions_after = 0;
    {
        running_service running(settings);
        auto& service = *running.service;
        REQUIRE(wait_for(service, service.wait_synced(30s)));
        auto const asked_before = blocks_asked(log_path, served);
        auto const received_before = service.current().bytes_received;
        sessions_before = peer.sessions.load();
        auto const history = wait_for(service, service.history(target));
        REQUIRE(history);
        REQUIRE(confirmed_txids(*history) == std::vector<hash_digest>{first_paid.hash(), second_paid.hash()});
        covered_bytes = service.current().bytes_received - received_before;
        sessions_after = peer.sessions.load();
        auto const asked = blocks_asked(log_path, served);
        for (auto const height : asked) {
            if ( ! asked_before.contains(height)) first_session.insert(height);
        }
        // Each needed block came with three others of its bucket.
        for (auto const real : {50u, 200u}) {
            REQUIRE(first_session.contains(real));
            auto const others = std::ranges::count_if(first_session, [&](uint32_t height) {
                return height != real && bucket(height) == bucket(real);
            });
            REQUIRE(others >= 3);
        }
        // Each over a connection of its own.
        REQUIRE(sessions_after - sessions_before >= first_session.size());

        // Asking for a transaction again downloads nothing.
        REQUIRE(wait_for(service, service.transaction(first_paid.hash(), std::nullopt)));
        auto const lines = getdata_lines(log_path);
        REQUIRE(wait_for(service, service.transaction(first_paid.hash(), std::nullopt)));
        REQUIRE(wait_for(service, service.merkle(first_paid.hash(), std::nullopt)));
        REQUIRE(getdata_lines(log_path) == lines);

        // RPA: every block of the range, so that none of the 65,536 prefixes
        // is told apart by what was downloaded.
        auto const asked_rpa_before = blocks_asked(log_path, served);
        auto const rpa_received_before = service.current().bytes_received;
        auto const prefix = chain::input_prefix(ground.inputs()[0]);
        auto const matches = wait_for(service, service.rpa_history(prefix, 16, 90, 110));
        REQUIRE(matches);
        REQUIRE(std::ranges::any_of(*matches, [&](auto const& match) { return match.txid == ground.hash(); }));
        auto const rpa_bytes = service.current().bytes_received - rpa_received_before;
        auto const asked_rpa = blocks_asked(log_path, served);
        std::set<uint32_t> downloaded;
        for (auto height = 90u; height <= 110u; ++height) {
            if (asked_rpa.contains(height) || asked_rpa_before.contains(height)) downloaded.insert(height);
        }
        REQUIRE(downloaded.size() == 21u);
        // A peer's test of every prefix: one whose blocks in the range are
        // not all among those downloaded is told apart.
        std::map<uint32_t, std::set<uint16_t>> counted;
        for (auto height = 90u; height <= 110u; ++height) {
            auto const& txs = served.blocks[height].transactions();
            for (size_t at = 1; at < txs.size(); ++at) {
                auto const& inputs = txs[at].inputs();
                for (size_t in = 0; in < std::min(inputs.size(), chain::input_prefix_inputs); ++in) {
                    counted[height].insert(chain::input_prefix(inputs[in]));
                }
            }
        }
        size_t told_apart = 0;
        uint64_t matching_bytes = 0;
        for (uint32_t value = 0; value <= 0xffff; ++value) {
            for (auto const& [height, values] : counted) {
                if (values.contains(uint16_t(value)) && ! downloaded.contains(height)) {
                    ++told_apart;
                    break;
                }
            }
        }
        REQUIRE(told_apart == 0u);
        for (auto height = 90u; height <= 110u; ++height) {
            auto const& txs = served.blocks[height].transactions();
            if (std::ranges::any_of(txs.begin() + 1, txs.end(), [&](auto const& tx) { return ! wallet::prefix_inputs(tx, prefix, 16).empty(); })) {
                matching_bytes += served.blocks[height].serialized_size();
            }
        }

        // The same blocks without cover, for the cost.
        temporary_directory bare_dir("privacy_bare");
        auto bare = settings_for(bare_dir.path, {peer.endpoint()});
        bare.isolate_downloads = false;
        bare.decoy_blocks = 0;
        running_service bare_running(bare);
        auto& bare_service = *bare_running.service;
        REQUIRE(wait_for(bare_service, bare_service.wait_synced(30s)));
        auto const bare_before = bare_service.current().bytes_received;
        REQUIRE(wait_for(bare_service, bare_service.history(target)));
        auto const bare_bytes = bare_service.current().bytes_received - bare_before;
        std::printf("wallet privacy cost: a history of 2 blocks in 300 took %llu bytes with decoys and isolation, "
            "%llu bytes without; an RPA range of 21 blocks took %llu bytes, %llu for its matching blocks alone\n",
            (unsigned long long)covered_bytes, (unsigned long long)bare_bytes, (unsigned long long)rpa_bytes,
            (unsigned long long)matching_bytes);
        std::fflush(stdout);
    }

    // After a restart, the same blocks bring the same decoys.
    std::filesystem::remove(log_path);
    {
        running_service running(settings);
        auto& service = *running.service;
        REQUIRE(wait_for(service, service.wait_synced(30s)));
        auto const asked_before = blocks_asked(log_path, served);
        REQUIRE(wait_for(service, service.history(target)));
        std::set<uint32_t> second_session;
        for (auto const height : blocks_asked(log_path, served)) {
            if ( ! asked_before.contains(height)) second_session.insert(height);
        }
        auto const in_buckets = [&](std::set<uint32_t> const& heights) {
            std::set<uint32_t> out;
            for (auto const height : heights) {
                if (bucket(height) == bucket(50) || bucket(height) == bucket(200)) out.insert(height);
            }
            return out;
        };
        REQUIRE(in_buckets(second_session) == in_buckets(first_session));
    }
}
