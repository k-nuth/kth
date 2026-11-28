// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CashTokens and BCMR in wallet support, with no index: a category rebuilt
// from the blocks that hold its outputs, and an identity's authchain followed
// spend by spend, on a chain the node stores.

#include "wallet_chain_fixture.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/node/wallet/electrum.hpp>
#include <kth/node/wallet/tokens.hpp>

namespace {

hash_digest digest_of(uint32_t seed) {
    uint8_t const bytes[] = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), uint8_t(seed >> 24), 0xd7};
    return sha256_hash(bytes);
}

chain::script script_of(data_chunk bytes) {
    return chain::script(bytes, false);
}

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

struct paid {
    uint64_t value;
    data_chunk script;
    chain::token_data_opt token{};
};

// A transaction that is never validated: what the pure functions read.
chain::transaction spending(std::vector<chain::point> const& previous, std::vector<paid> const& outputs, uint32_t salt) {
    chain::input::list inputs;
    for (auto const& point : previous) {
        inputs.emplace_back(chain::output_point{point.hash(), point.index()}, script_of({0x01, uint8_t(salt), uint8_t(salt >> 8)}), 0xfffffffe);
    }
    chain::output::list made;
    for (auto const& out : outputs) made.emplace_back(out.value, script_of(out.script), out.token);
    return chain::transaction(2, 0, std::move(inputs), std::move(made));
}

// A spend of outputs the miner's key locks, each input signed with it, as
// the node validates it.
chain::transaction signed_spending(std::vector<std::pair<chain::transaction, uint32_t>> const& previous,
        std::vector<paid> const& outputs) {
    auto const flags = blockchain::settings(domain::config::network::regtest).enabled_flags();
    chain::input::list inputs;
    for (auto const& [tx, index] : previous) {
        inputs.emplace_back(chain::output_point{tx.hash(), index}, chain::script{}, 0xffffffff);
    }
    chain::output::list made;
    for (auto const& out : outputs) made.emplace_back(out.value, script_of(out.script), out.token);
    chain::transaction unsigned_tx(2, 0, std::move(inputs), std::move(made));
    // The outputs being spent, where the signature hash reads them (their
    // token data included).
    for (size_t at = 0; at < previous.size(); ++at) {
        unsigned_tx.inputs()[at].previous_output().validation.cache = previous[at].first.outputs()[previous[at].second];
    }
    chain::input::list signed_inputs;
    for (size_t at = 0; at < previous.size(); ++at) {
        auto const& [tx, index] = previous[at];
        auto const endorsement = chain::script::create_endorsement(test::miner_secret(), test::miner_script(), unsigned_tx,
            uint32_t(at), uint8_t(infrastructure::machine::sighash_algorithm::forkid_all), flags, tx.outputs()[index].value());
        REQUIRE(endorsement);
        signed_inputs.emplace_back(chain::output_point{tx.hash(), index},
            chain::script(chain::script::to_pay_public_key_hash_pattern_unlocking(*endorsement, test::miner_public())),
            0xffffffff);
    }
    return chain::transaction(2, 0, std::move(signed_inputs), chain::output::list(unsigned_tx.outputs()));
}

// A category and two identities on a chain of 170 blocks the node stores and
// validates. Every output spent later is the miner's, so it can be signed.
//   101 P: the authbase, spending block 1's coinbase; its hash is the category id.
//   105 G: the genesis, spending P's output 0: a minting NFT with 50 reserved
//          (the identity output), 1000 fungible, a mutable and an immutable NFT.
//   107 M: 600 and 300 of the 1000 (100 burned), and the mutable NFT burned.
//   150 U1: the identity output moved on, a publication, and a new NFT minted.
//   160 U2: moved on again, a publication with two URIs, and a second one,
//           which the specification ignores.
//   102 Q, 110 R: a second identity, burned by S, which the test puts in the
//           node's mempool.
struct token_chain {
    test::chain_fixture fixture;
    std::vector<chain::block> blocks;
    hash_digest first_registry = registry_hash("first");
    hash_digest second_registry = registry_hash("second");
    hash_digest ignored_registry = registry_hash("ignored");
    chain::transaction base;
    hash_digest category{};
    chain::transaction genesis;
    chain::transaction move;
    chain::transaction first_update;
    chain::transaction second_update;
    chain::transaction other_base;
    chain::transaction other_step;
    chain::transaction burn;

    token_chain() : fixture("wallet_tokens_chain") {
        auto const miner = test::miner_script().bytes();
        auto const length = 170u;
        auto const base_time = uint32_t(zulu_time()) - (length + 30) * test::block_spacing;
        auto previous = chain::block::genesis_regtest().hash();
        for (uint32_t height = 1; height <= length; ++height) {
            std::vector<chain::transaction> txs;
            if (height == 101) {
                base = signed_spending({{blocks[0].transactions().front(), 0}}, {{5000, miner}});
                category = base.hash();
                txs.push_back(base);
            }
            if (height == 102) {
                other_base = signed_spending({{blocks[1].transactions().front(), 0}}, {{4000, miner}});
                txs.push_back(other_base);
            }
            if (height == 105) {
                genesis = signed_spending({{base, 0}}, {
                    {1000, miner, chain::make_both(category, 50, chain::capability_t::minting, {})},
                    {1000, miner, chain::make_fungible(category, 1000)},
                    {1000, miner, chain::make_non_fungible(category, chain::capability_t::mut, {0x01})},
                    {1000, lock(0x23), chain::make_non_fungible(category, chain::capability_t::none, {0x02})},
                });
                txs.push_back(genesis);
            }
            if (height == 107) {
                move = signed_spending({{genesis, 1}, {genesis, 2}}, {
                    {1000, lock(0x24), chain::make_fungible(category, 600)},
                    {500, lock(0x25), chain::make_fungible(category, 300)},
                });
                txs.push_back(move);
            }
            if (height == 110) {
                other_step = signed_spending({{other_base, 0}}, {{3000, miner}});
                txs.push_back(other_step);
            }
            if (height == 150) {
                first_update = signed_spending({{genesis, 0}}, {
                    {600, miner, chain::make_both(category, 50, chain::capability_t::minting, {})},
                    {0, publication(first_registry, {"example.test/first.json"})},
                    {300, lock(0x26), chain::make_non_fungible(category, chain::capability_t::none, {0x03})},
                });
                txs.push_back(first_update);
            }
            if (height == 160) {
                second_update = signed_spending({{first_update, 0}}, {
                    {500, miner, chain::make_both(category, 50, chain::capability_t::minting, {})},
                    {0, publication(second_registry, {"example.test/second.json", "ipfs://bafysecond"})},
                    {0, publication(ignored_registry, {"example.test/ignored.json"})},
                });
                txs.push_back(second_update);
            }
            blocks.push_back(test::mine_block(previous, height, base_time + height * test::block_spacing, 7, std::move(txs), 0));
            previous = blocks.back().hash();
        }
        burn = signed_spending({{other_step, 0}}, {{0, data_chunk{0x6a}}, {2000, lock(0x33)}});

        REQUIRE(fixture.created());
        REQUIRE(fixture.start());
        REQUIRE(fixture.organizer().add_headers(test::headers_of(blocks)).headers_added == blocks.size());
        test::persist_headers(fixture, blocks, 1);
        test::connect_bodies(fixture, blocks, 1);
        REQUIRE(fixture.chain().chain_view()->connected_tip_height == length);
    }
};

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

TEST_CASE("wallet tokens from the node's own chain: a category rebuilt, identities followed spend by spend", "[wallet][wallet_tokens]") {
    token_chain made;
    auto& node_chain = made.fixture.chain();
    auto own = wallet::service::from_chain(node_chain, domain::config::network::regtest);
    auto& service = *own;

    // The identity: its authchain, its definitive publication and the one
    // the specification ignores.
    auto const found = wait_for(service, service.resolve_identity(made.base.hash()));
    REQUIRE(found);
    REQUIRE(txids_of(*found) == std::vector<hash_digest>{made.base.hash(), made.genesis.hash(),
        made.first_update.hash(), made.second_update.hash()});
    REQUIRE(found->chain.back().height == 160u);
    REQUIRE(found->chain.back().from == wallet::identity_step::source::chain);
    REQUIRE( ! found->burned);
    REQUIRE(found->publication);
    REQUIRE(found->publication->output == 1u);
    REQUIRE(found->publication->registry_hash == made.second_registry);
    REQUIRE(found->publication->uris == std::vector<std::string>{"example.test/second.json", "ipfs://bafysecond"});
    REQUIRE(found->ignored.size() == 1u);
    REQUIRE(found->ignored[0].registry_hash == made.ignored_registry);
    // Asked again, the same.
    auto const again = wait_for(service, service.resolve_identity(made.base.hash()));
    REQUIRE(again);
    REQUIRE(txids_of(*again) == txids_of(*found));

    // A registry's authchain extension skips what it holds; one that
    // diverged is refused.
    auto const extended = wait_for(service, service.resolve_identity(made.base.hash(), {made.base, made.genesis}));
    REQUIRE(extended);
    REQUIRE(txids_of(*extended) == txids_of(*found));
    REQUIRE(extended->chain.front().from == wallet::identity_step::source::registry);
    auto const diverged = wait_for(service, service.resolve_identity(made.base.hash(), {made.base, made.move}));
    REQUIRE( ! diverged);
    REQUIRE(diverged.error() == error::bad_stream);

    // The second identity, not burned yet; then burned in the node's mempool.
    auto const standing = wait_for(service, service.resolve_identity(made.other_base.hash()));
    REQUIRE(standing);
    REQUIRE( ! standing->burned);
    REQUIRE(txids_of(*standing) == std::vector<hash_digest>{made.other_base.hash(), made.other_step.hash()});
    auto const pooled = std::make_shared<message::transaction const>(made.burn);
    auto submitted = ::asio::co_spawn(node_chain.executor(), [&node_chain, pooled]() -> ::asio::awaitable<code> {
        co_return co_await node_chain.submit_transaction(pooled);
    }, ::asio::use_future);
    REQUIRE(submitted.wait_for(60s) == std::future_status::ready);
    REQUIRE(submitted.get() == error::success);
    auto const burned = wait_for(service, service.resolve_identity(made.other_base.hash()));
    REQUIRE(burned);
    REQUIRE(burned->burned);
    REQUIRE(txids_of(*burned) == std::vector<hash_digest>{made.other_base.hash(), made.other_step.hash(), made.burn.hash()});
    REQUIRE(burned->chain.back().from == wallet::identity_step::source::mempool);

    // The category, rebuilt from its outputs.
    auto const state = wait_for(service, service.category(made.category));
    REQUIRE(state);
    REQUIRE(state->genesis == made.genesis.hash());
    REQUIRE(state->genesis_height == 105u);
    REQUIRE(state->supply.total == 950u);
    REQUIRE(state->supply.reserved == 50u);
    REQUIRE(state->supply.circulating == 900u);
    REQUIRE(state->holders == 5u);
    REQUIRE(state->nfts == 3u);
    std::set<chain::point> outpoints;
    for (auto const& coin : state->unspent) outpoints.insert(coin.outpoint);
    REQUIRE(outpoints == std::set<chain::point>{chain::point(made.second_update.hash(), 0), chain::point(made.move.hash(), 0),
        chain::point(made.move.hash(), 1), chain::point(made.genesis.hash(), 3), chain::point(made.first_update.hash(), 2)});
    // A category no output holds has nothing.
    auto const none = wait_for(service, service.category(digest_of(99)));
    REQUIRE(none);
    REQUIRE(none->unspent.empty());
    REQUIRE( ! none->genesis);

    // What a holder has of it.
    auto const held = wait_for(service, service.holdings(wallet::target::of_script(lock(0x24))));
    REQUIRE(held);
    REQUIRE(held->size() == 1u);
    REQUIRE(held->front().category == made.category);
    REQUIRE(held->front().fungible == 600u);

    // The same through the Electrum listener, as kth's own methods.
    wallet::electrum_server electrum(service, domain::config::network::regtest);
    auto const ask = [&](std::string const& method, std::string const& params) {
        return wait_for(service, electrum.answer(R"({"jsonrpc":"2.0","id":3,"method":")" + method + R"(","params":)" + params + "}"));
    };
    auto const shown = ask("kth.token.category", "[\"" + encode_hash(made.category) + "\"]");
    REQUIRE(shown.find(R"("genesis":{"tx_hash":")" + encode_hash(made.genesis.hash()) + R"(","height":105})") != std::string::npos);
    REQUIRE(shown.find(R"("supply":{"total":"950","reserved":"50","circulating":"900"})") != std::string::npos);
    REQUIRE(shown.find(R"("holders":5,"nfts":3)") != std::string::npos);
    auto const resolved = ask("kth.identity.resolve", "[\"" + encode_hash(made.base.hash()) + "\"]");
    REQUIRE(resolved.find(R"("authhead":")" + encode_hash(made.second_update.hash()) + "\"") != std::string::npos);
    REQUIRE(resolved.find(R"("hash":")" + encode_base16(made.second_registry) + R"(","uris":["example.test/second.json","ipfs://bafysecond"]})") != std::string::npos);
    REQUIRE(ask("kth.token.category", R"(["zz"])").find(R"("code":-32602)") != std::string::npos);
    REQUIRE(ask("kth.identity.resolve", "[\"" + encode_hash(made.base.hash()) + R"(","00"])").find(R"("code":-32602)") != std::string::npos);
    REQUIRE(ask("kth.token.holdings", "[1]").find(R"("code":-32602)") != std::string::npos);
}
