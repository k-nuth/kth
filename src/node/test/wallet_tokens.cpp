// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CashTokens and BCMR in wallet support, with no index: a category rebuilt
// from the blocks its key matches, and an identity's authchain followed spend
// by spend, against peers in this process.

#include "wallet_peer_harness.hpp"

#include <algorithm>
#include <set>
#include <string>

#include <kth/node/wallet/electrum.hpp>
#include <kth/node/wallet/tokens.hpp>

namespace {

// A P2PKH-shaped locking script of its own for each tag.
data_chunk lock(uint8_t tag) {
    data_chunk out{0x76, 0xa9, 0x14};
    out.insert(out.end(), 20, tag);
    out.push_back(0x88);
    out.push_back(0xac);
    return out;
}

// OP_RETURN <'BCMR'> <hash> <uri>...
data_chunk publication(hash_digest const& hash, std::vector<std::string> const& uris) {
    data_chunk out{0x6a, 0x04, 'B', 'C', 'M', 'R', 0x20};
    out.insert(out.end(), hash.begin(), hash.end());
    for (auto const& uri : uris) {
        out.push_back(uint8_t(uri.size()));
        out.insert(out.end(), uri.begin(), uri.end());
    }
    return out;
}

hash_digest registry_hash(std::string_view registry) {
    return sha256_hash(byte_span(reinterpret_cast<uint8_t const*>(registry.data()), registry.size()));
}

std::vector<hash_digest> txids_of(wallet::identity const& found) {
    std::vector<hash_digest> out;
    for (auto const& step : found.chain) out.push_back(step.txid);
    return out;
}

} // namespace

TEST_CASE("wallet tokens: BCMR publication outputs, registry bytes and authchain extensions", "[wallet][wallet_tokens]") {
    auto const first = registry_hash(R"({"version":1})");
    auto const second = registry_hash(R"({"version":2})");
    auto const tx = spending({chain::point(digest_of(1), 0)}, {
        {1000, lock(1)},
        {0, publication(first, {"example.test/a.json", "ipfs://bafy"})},
        {0, publication(second, {})},
        // Not a publication: the hash is not a push of 32 bytes.
        {0, data_chunk{0x6a, 0x04, 'B', 'C', 'M', 'R', 0x01, 0x00}},
        // Not a publication: a URI push cut short.
        {0, [&] { auto bytes = publication(first, {}); bytes.push_back(0x05); bytes.push_back('x'); return bytes; }()},
    }, 1);
    auto const found = wallet::bcmr_publications(tx);
    REQUIRE(found.size() == 2u);
    REQUIRE(found[0].output == 1u);
    REQUIRE(found[0].registry_hash == first);
    REQUIRE(found[0].uris == std::vector<std::string>{"example.test/a.json", "ipfs://bafy"});
    REQUIRE(found[1].output == 2u);
    REQUIRE(found[1].uris.empty());

    std::string const registry = R"({"version":1})";
    REQUIRE(wallet::registry_matches(byte_span(reinterpret_cast<uint8_t const*>(registry.data()), registry.size()), first));
    REQUIRE( ! wallet::registry_matches(byte_span(reinterpret_cast<uint8_t const*>(registry.data()), registry.size()), second));

    auto const base = spending({chain::point(digest_of(2), 0)}, {{1000, lock(2)}}, 2);
    auto const next = spending({chain::point(base.hash(), 0)}, {{900, lock(3)}}, 3);
    auto const after = spending({chain::point(digest_of(4), 0), chain::point(next.hash(), 0)}, {{800, lock(4)}}, 4);
    auto const checked = wallet::verify_authchain(base.hash(), {base, next, after});
    REQUIRE(checked);
    REQUIRE(*checked == std::vector<hash_digest>{base.hash(), next.hash(), after.hash()});
    // Another authbase, a link that spends output 1, an empty extension.
    REQUIRE( ! wallet::verify_authchain(next.hash(), {base, next}));
    auto const sideways = spending({chain::point(base.hash(), 1)}, {{900, lock(5)}}, 5);
    REQUIRE( ! wallet::verify_authchain(base.hash(), {base, sideways}));
    REQUIRE( ! wallet::verify_authchain(base.hash(), {}));
}

TEST_CASE("wallet tokens: supply and holdings from unspent outputs", "[wallet][wallet_tokens]") {
    auto const category = digest_of(10);
    auto const other = digest_of(11);
    std::vector<wallet::utxo> unspent{
        {chain::point(digest_of(20), 0), 1000, chain::make_both(category, 50, chain::capability_t::minting, {}), lock(1), 5},
        {chain::point(digest_of(21), 0), 1000, chain::make_fungible(category, 600), lock(2), 7},
        {chain::point(digest_of(22), 0), 1000, chain::make_both(category, 30, chain::capability_t::mut, {0x01}), lock(3), 8},
        {chain::point(digest_of(23), 0), 1000, chain::make_non_fungible(category, chain::capability_t::none, {0x02}), lock(2), 9},
        {chain::point(digest_of(24), 0), 1000, chain::make_fungible(category, 70), lock(4), std::nullopt},
        {chain::point(digest_of(25), 0), 1000, chain::make_fungible(other, 5), lock(4), 9},
    };
    auto const confirmed = wallet::supply_of(category, unspent, false);
    REQUIRE(confirmed.total == 680u);
    REQUIRE(confirmed.reserved == 80u);
    REQUIRE(confirmed.circulating == 600u);
    auto const all = wallet::supply_of(category, unspent, true);
    REQUIRE(all.total == 750u);
    REQUIRE(all.circulating == 670u);

    auto const held = wallet::holdings_of(unspent);
    REQUIRE(held.size() == 2u);
    auto const mine = std::ranges::find_if(held, [&](auto const& one) { return one.category == category; });
    REQUIRE(mine != held.end());
    REQUIRE(mine->fungible == 750u);
    REQUIRE(mine->nfts == 3u);
}

namespace {

// A category and two identities on a chain of 300 blocks (two whole buckets
// of 144 and part of a third).
//   3   P: the authbase; its hash is the category id.
//   5   G: the genesis, spending P's output 0: a minting NFT with 50 reserved
//          (the identity output), 1000 fungible, a mutable and an immutable NFT.
//   7   M: 600 and 300 of the 1000 (100 burned), and the mutable NFT burned.
//   150 U1: the identity output moved on, a publication, and a new NFT minted.
//   160 U2: moved on again, a publication with two URIs, and a second one,
//          which the specification ignores.
//   20  Q, 30 R: a second identity, burned by S in the mempool.
struct token_chain {
    served_chain served;
    chain::transaction base = spending({chain::point(digest_of(500), 0)}, {{5000, lock(0x0a)}}, 1);
    hash_digest category = base.hash();
    hash_digest first_registry = registry_hash("first");
    hash_digest second_registry = registry_hash("second");
    hash_digest ignored_registry = registry_hash("ignored");
    chain::transaction genesis = spending({chain::point(base.hash(), 0)}, {
        {1000, lock(0x0b), chain::make_both(category, 50, chain::capability_t::minting, {})},
        {1000, lock(0x21), chain::make_fungible(category, 1000)},
        {1000, lock(0x22), chain::make_non_fungible(category, chain::capability_t::mut, {0x01})},
        {1000, lock(0x23), chain::make_non_fungible(category, chain::capability_t::none, {0x02})},
    }, 2);
    chain::transaction move = spending({chain::point(genesis.hash(), 1), chain::point(genesis.hash(), 2)}, {
        {1000, lock(0x24), chain::make_fungible(category, 600)},
        {1000, lock(0x25), chain::make_fungible(category, 300)},
    }, 3);
    chain::transaction first_update = spending({chain::point(genesis.hash(), 0)}, {
        {1000, lock(0x0c), chain::make_both(category, 50, chain::capability_t::minting, {})},
        {0, publication(first_registry, {"example.test/first.json"})},
        {1000, lock(0x26), chain::make_non_fungible(category, chain::capability_t::none, {0x03})},
    }, 4);
    chain::transaction second_update = spending({chain::point(first_update.hash(), 0)}, {
        {1000, lock(0x0d), chain::make_both(category, 50, chain::capability_t::minting, {})},
        {0, publication(second_registry, {"example.test/second.json", "ipfs://bafysecond"})},
        {0, publication(ignored_registry, {"example.test/ignored.json"})},
    }, 5);
    chain::transaction other_base = spending({chain::point(digest_of(501), 0)}, {{4000, lock(0x31)}}, 6);
    chain::transaction other_step = spending({chain::point(other_base.hash(), 0)}, {{3000, lock(0x32)}}, 7);
    chain::transaction burn = spending({chain::point(other_step.hash(), 0)}, {{0, data_chunk{0x6a}}, {2000, lock(0x33)}}, 8);

    token_chain() {
        for (uint32_t height = 1; height <= 300; ++height) {
            std::vector<chain::transaction> txs;
            if (height == 3) txs.push_back(base);
            if (height == 5) txs.push_back(genesis);
            if (height == 7) txs.push_back(move);
            if (height == 20) txs.push_back(other_base);
            if (height == 30) txs.push_back(other_step);
            if (height == 150) txs.push_back(first_update);
            if (height == 160) txs.push_back(second_update);
            served.mine(std::move(txs));
        }
        served.pooled.push_back(burn);
    }
};

} // namespace

TEST_CASE("wallet tokens from peers: a category rebuilt, identities followed spend by spend, with the usual cover", "[wallet][wallet_tokens]") {
    token_chain made;
    test_peer peer(made.served);
    temporary_directory dir("tokens");
    auto const log_path = dir.path / "sent.log";
    auto settings = settings_for(dir.path, {peer.endpoint()});
    settings.sent_log = log_path;

    {
        running_service running(settings);
        auto& service = *running.service;
        REQUIRE(wait_for(service, service.wait_synced(30s)));
        auto const synced_blocks = blocks_asked(log_path, made.served);

        // The identity: its authchain, its definitive publication and the one
        // the specification ignores.
        auto const found = wait_for(service, service.resolve_identity(made.base.hash()));
        REQUIRE(found);
        REQUIRE(txids_of(*found) == std::vector<hash_digest>{made.base.hash(), made.genesis.hash(),
            made.first_update.hash(), made.second_update.hash()});
        REQUIRE(found->chain.back().height == 160u);
        REQUIRE( ! found->burned);
        REQUIRE(found->publication);
        REQUIRE(found->publication->output == 1u);
        REQUIRE(found->publication->registry_hash == made.second_registry);
        REQUIRE(found->publication->uris == std::vector<std::string>{"example.test/second.json", "ipfs://bafysecond"});
        REQUIRE(found->ignored.size() == 1u);
        REQUIRE(found->ignored[0].registry_hash == made.ignored_registry);

        // Each block the walk read came with three others of its bucket.
        std::set<uint32_t> walked;
        for (auto const height : blocks_asked(log_path, made.served)) {
            if ( ! synced_blocks.contains(height)) walked.insert(height);
        }
        for (auto const real : {150u, 160u}) {
            REQUIRE(walked.contains(real));
            auto const others = std::ranges::count_if(walked, [&](uint32_t height) { return height != real && height / 144 == real / 144; });
            REQUIRE(others >= 3);
        }

        // A burned identity, its last step in the mempool.
        auto burned = wait_for(service, service.resolve_identity(made.other_base.hash()));
        auto const deadline = std::chrono::steady_clock::now() + 30s;
        auto seen = wait_for(service, service.changed(0, 0s));
        while (burned && ! burned->burned && std::chrono::steady_clock::now() < deadline) {
            seen = wait_for(service, service.changed(seen, 5s));
            burned = wait_for(service, service.resolve_identity(made.other_base.hash()));
        }
        REQUIRE(burned);
        REQUIRE(burned->burned);
        REQUIRE(txids_of(*burned) == std::vector<hash_digest>{made.other_base.hash(), made.other_step.hash(), made.burn.hash()});
        REQUIRE(burned->chain.back().from == wallet::identity_step::source::mempool);

        // A registry's authchain extension skips what it holds; one that
        // diverged is refused.
        auto const extended = wait_for(service, service.resolve_identity(made.base.hash(), {made.base, made.genesis}));
        REQUIRE(extended);
        REQUIRE(txids_of(*extended) == txids_of(*found));
        REQUIRE(extended->chain.front().from == wallet::identity_step::source::registry);
        auto const diverged = wait_for(service, service.resolve_identity(made.base.hash(), {made.base, made.move}));
        REQUIRE( ! diverged);
        REQUIRE(diverged.error() == error::bad_stream);

        // The category, rebuilt from its outputs.
        auto const state = wait_for(service, service.category(made.category));
        REQUIRE(state);
        REQUIRE(state->genesis == made.genesis.hash());
        REQUIRE(state->genesis_height == 5u);
        REQUIRE(state->supply.total == 950u);
        REQUIRE(state->supply.reserved == 50u);
        REQUIRE(state->supply.circulating == 900u);
        REQUIRE(state->holders == 5u);
        REQUIRE(state->nfts == 3u);
        std::set<chain::point> outpoints;
        for (auto const& coin : state->unspent) outpoints.insert(coin.outpoint);
        REQUIRE(outpoints == std::set<chain::point>{chain::point(made.second_update.hash(), 0), chain::point(made.move.hash(), 0),
            chain::point(made.move.hash(), 1), chain::point(made.genesis.hash(), 3), chain::point(made.first_update.hash(), 2)});

        // What a holder has of it.
        auto const held = wait_for(service, service.holdings(wallet::target::of_script(lock(0x24))));
        REQUIRE(held);
        REQUIRE(held->size() == 1u);
        REQUIRE(held->front().fungible == 600u);

        // The same through the Electrum listener, as kth's own methods.
        wallet::electrum_server electrum(service, domain::config::network::regtest);
        auto const ask = [&](std::string const& method, std::string const& params) {
            return wait_for(service, electrum.answer(R"({"jsonrpc":"2.0","id":3,"method":")" + method + R"(","params":)" + params + "}"));
        };
        auto const shown = ask("kth.token.category", "[\"" + encode_hash(made.category) + "\"]");
        REQUIRE(shown.find(R"("genesis":{"tx_hash":")" + encode_hash(made.genesis.hash()) + R"(","height":5})") != std::string::npos);
        REQUIRE(shown.find(R"("supply":{"total":"950","reserved":"50","circulating":"900"})") != std::string::npos);
        REQUIRE(shown.find(R"("holders":5,"nfts":3)") != std::string::npos);
        auto const resolved = ask("kth.identity.resolve", "[\"" + encode_hash(made.base.hash()) + "\"]");
        REQUIRE(resolved.find(R"("authhead":")" + encode_hash(made.second_update.hash()) + "\"") != std::string::npos);
        REQUIRE(resolved.find(R"("hash":")" + encode_base16(made.second_registry) + R"(","uris":["example.test/second.json","ipfs://bafysecond"]})") != std::string::npos);
        REQUIRE(ask("kth.token.category", R"(["zz"])").find(R"("code":-32602)") != std::string::npos);
        REQUIRE(ask("kth.identity.resolve", "[\"" + encode_hash(made.base.hash()) + R"(","00"])").find(R"("code":-32602)") != std::string::npos);
        REQUIRE(ask("kth.token.holdings", "[1]").find(R"("code":-32602)") != std::string::npos);

        // Nothing sent names the category, an identity or a locking script.
        std::vector<std::string> const secrets{encode_hash(made.category), encode_base16(made.category),
            encode_base16(chain::category_key(made.category)), encode_base16(chain::script_key(lock(0x0d))),
            encode_base16(lock(0x0d)), encode_base16(made.other_base.hash())};
        std::ifstream log(log_path);
        std::string line;
        while (std::getline(log, line)) {
            for (auto const& secret : secrets) REQUIRE(line.find(secret) == std::string::npos);
        }
    }

    // After a restart the authchain read before is kept: the walk goes on
    // from the authhead, and the blocks before it are not read again.
    std::filesystem::remove(log_path);
    {
        running_service running(settings);
        auto& service = *running.service;
        REQUIRE(wait_for(service, service.wait_synced(30s)));
        auto const synced_blocks = blocks_asked(log_path, made.served);
        auto const again = wait_for(service, service.resolve_identity(made.base.hash()));
        REQUIRE(again);
        REQUIRE(txids_of(*again).size() == 4u);
        std::set<uint32_t> read;
        for (auto const height : blocks_asked(log_path, made.served)) {
            if ( ! synced_blocks.contains(height)) read.insert(height);
        }
        // The genesis' bucket is not read again: the walk starts at the authhead.
        REQUIRE(std::ranges::none_of(read, [](uint32_t height) { return height < 144; }));
    }
}
