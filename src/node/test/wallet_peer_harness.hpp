// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_TEST_WALLET_PEER_HARNESS_HPP
#define KTH_NODE_TEST_WALLET_PEER_HARNESS_HPP

// Peers in the test process for wallet support: each listens on a port the
// OS assigns, serves a regtest chain with its script and input prefix
// filters, and answers what a Knuth node answers.

#include <test_helpers.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/domain/chain/golomb_set.hpp>
#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/domain/chain/script_filter_builder.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/wallet/service.hpp>

#include "sync_harness.hpp"
#include "../../blockchain/test/regtest_miner.hpp"

using namespace kth;
using namespace std::chrono_literals;
namespace chain = kth::domain::chain;
namespace message = kth::domain::message;
namespace wallet = kth::node::wallet;
using tcp = ::asio::ip::tcp;

namespace {

struct temporary_directory {
    std::filesystem::path path;
    explicit temporary_directory(std::string_view tag)
        : path(std::filesystem::temp_directory_path() / (std::string("kth_wallet_service_") + std::string(tag))) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
        std::filesystem::create_directories(path);
    }
    ~temporary_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

inline chain::script script_of(data_chunk bytes) {
    return chain::script(bytes, false);
}

inline hash_digest digest_of(uint32_t seed) {
    uint8_t const bytes[] = {uint8_t(seed), uint8_t(seed >> 8), uint8_t(seed >> 16), uint8_t(seed >> 24), 0xa5};
    return sha256_hash(bytes);
}

// Static data: decoded without assertions, which belong to test cases.
inline data_chunk decoded(std::string_view hex) {
    auto bytes = decode_base16(hex);
    return bytes ? std::move(*bytes) : data_chunk{};
}

inline data_chunk const watched_script = decoded("76a914111111111111111111111111111111111111111188ac");
inline data_chunk const other_script = decoded("76a914222222222222222222222222222222222222222288ac");

struct paid {
    uint64_t value;
    data_chunk script;
    chain::token_data_opt token{};
};

inline chain::transaction spending(std::vector<chain::point> const& previous, std::vector<paid> const& outputs, uint32_t salt) {
    chain::input::list inputs;
    for (auto const& point : previous) {
        inputs.emplace_back(chain::output_point{point.hash(), point.index()}, script_of({0x01, uint8_t(salt), uint8_t(salt >> 8)}), 0xfffffffe);
    }
    chain::output::list made;
    for (auto const& out : outputs) made.emplace_back(out.value, script_of(out.script), out.token);
    return chain::transaction(2, 0, std::move(inputs), std::move(made));
}

// A regtest chain with the filters a node serves for it.
struct served_chain {
    std::vector<chain::block> blocks;
    std::map<hash_digest, uint32_t> heights;
    std::map<chain::point, chain::output> created;
    std::vector<std::vector<chain::output>> spent;
    std::array<std::vector<data_chunk>, 3> filters;
    std::array<std::vector<hash_digest>, 3> hashes;
    std::array<std::vector<hash_digest>, 3> headers;
    // Announced to every client after the handshake, and served on request.
    std::vector<chain::transaction> pooled;
    std::vector<message::double_spend_proof> proofs;
    // Sent to every client without being announced or asked for.
    std::vector<chain::transaction> pushed;
    // Checkpoint lists one short.
    bool short_checkpoints = false;
    uint32_t base_time = uint32_t(std::time(nullptr)) - 4000u * 600u;

    served_chain() {
        add(chain::block::genesis_regtest(), {});
    }

    void add(chain::block blk, std::vector<chain::output> spends) {
        auto const height = uint32_t(blocks.size());
        heights[blk.hash()] = height;
        for (auto const& tx : blk.transactions()) {
            for (uint32_t vout = 0; vout < tx.outputs().size(); ++vout) {
                created.insert_or_assign(chain::point(tx.hash(), vout), tx.outputs()[vout]);
            }
        }
        auto const script = chain::build_script_filter(blk, spends);
        REQUIRE(script);
        put(1, chain::to_data(*script));
        put(2, chain::to_data(chain::build_input_prefix_filter(blk)));
        spent.push_back(std::move(spends));
        blocks.push_back(std::move(blk));
    }

    void put(uint8_t type, data_chunk filter) {
        auto const previous = headers[type].empty() ? null_hash : headers[type].back();
        auto const hash = bitcoin_hash(filter);
        data_chunk pair(hash.begin(), hash.end());
        pair.insert(pair.end(), previous.begin(), previous.end());
        headers[type].push_back(bitcoin_hash(pair));
        hashes[type].push_back(hash);
        filters[type].push_back(std::move(filter));
    }

    // Mines the next block with `txs` after its coinbase.
    void mine(std::vector<chain::transaction> txs) {
        mine(std::move(txs), uint32_t(blocks.size()));
    }

    // With a salt of its own: another branch's block at the same height.
    void mine(std::vector<chain::transaction> txs, uint32_t salt) {
        auto const height = uint32_t(blocks.size());
        append(test::mine_block(blocks.back().hash(), height, base_time + height * 600, salt, std::move(txs), 0));
    }

    // A block made elsewhere; an output it spends that this chain did not
    // make is taken for one of another script.
    void append(chain::block blk) {
        std::vector<chain::output> spends;
        auto const& txs = blk.transactions();
        for (size_t at = 1; at < txs.size(); ++at) {
            for (auto const& in : txs[at].inputs()) {
                auto const found = created.find(chain::point(in.previous_output().hash(), in.previous_output().index()));
                spends.push_back(found != created.end() ? found->second
                    : chain::output(1, script_of(other_script), chain::token_data_opt{}));
            }
        }
        add(std::move(blk), std::move(spends));
    }

    // Replaces a block's served script filter: the filter header chain
    // follows the new bytes, so only the block itself can show it is wrong.
    void replace_script_filter(uint32_t height, data_chunk filter) {
        auto saved = filters[1];
        saved[height] = std::move(filter);
        filters[1].clear();
        hashes[1].clear();
        headers[1].clear();
        for (auto& bytes : saved) put(1, std::move(bytes));
    }

    uint32_t tip() const { return uint32_t(blocks.size() - 1); }
};

inline network::settings server_settings() {
    network::settings settings(domain::config::network::regtest);
    settings.user_agent = "/WalletTestPeer:1/";
    settings.relay_transactions = false;
    settings.services = uint64_t(message::version::service::node_network) |
        uint64_t(message::version::service::node_compact_filters);
    settings.channel_handshake_seconds = 5;
    return settings;
}

// One in-process peer: listens on a port the OS assigns and answers headers,
// filters and blocks from its chain.
struct test_peer {
    // The chain it serves; switch_to() gives it another.
    served_chain const* current;
    // Its sessions, to announce a switch to.
    std::vector<std::weak_ptr<network::peer_session>> live;
    ::asio::io_context io;
    tcp::acceptor acceptor{io, tcp::endpoint(::asio::ip::make_address("127.0.0.1"), 0)};
    std::thread thread;
    std::atomic<size_t> sessions{0};
    // What it announced and served from its mempool, for a failing test to show.
    std::atomic<size_t> announcements{0};
    std::atomic<size_t> served_transactions{0};
    std::atomic<size_t> served_proofs{0};
    // A peer that serves headers and blocks but no filters, as BCHN.
    bool filters = true;
    // Set the first time it stays silent for a block it does not have.
    std::promise<void> silent;
    std::atomic<bool> was_silent{false};

    explicit test_peer(served_chain const& served, bool serves_filters = true) : current(&served), filters(serves_filters) {
        ::asio::co_spawn(io, accept(), ::asio::detached);
        thread = std::thread([this] { io.run(); });
    }

    ~test_peer() {
        ::asio::post(io, [this] {
            std::error_code ignored;
            acceptor.close(ignored);
        });
        io.stop();
        thread.join();
    }

    // Bound before the client is made: the port is ready when this returns.
    std::string endpoint() const {
        return "127.0.0.1:" + std::to_string(acceptor.local_endpoint().port());
    }

    ::asio::awaitable<void> accept() {
        for (;;) {
            auto [ec, socket] = co_await acceptor.async_accept(::asio::as_tuple(::asio::use_awaitable));
            if (ec) co_return;
            ++sessions;
            ::asio::co_spawn(io, serve(std::move(socket)), ::asio::detached);
        }
    }

    served_chain const& chain() const { return *current; }

    // Serves `next` from now on (a heavier branch, say) and announces its
    // tip to every client; returns once that is done.
    void switch_to(served_chain const& next) {
        std::promise<void> done;
        ::asio::post(io, [this, &next, &done] {
            current = &next;
            for (auto const& weak : live) {
                auto peer = weak.lock();
                if ( ! peer) continue;
                auto const announced = message::inventory::create(message::inventory_vector::list{
                    message::inventory_vector(message::inventory_vector::type_id::block, next.blocks.back().hash())});
                if (announced) ::asio::co_spawn(io, [peer, inv = *announced]() -> ::asio::awaitable<void> {
                    (void)co_await peer->send(inv);
                }, ::asio::detached);
            }
            done.set_value();
        });
        done.get_future().wait();
    }

    std::optional<uint32_t> height_of(hash_digest const& hash) const {
        auto const found = chain().heights.find(hash);
        if (found == chain().heights.end()) return std::nullopt;
        return found->second;
    }

    ::asio::awaitable<void> serve(tcp::socket socket) {
        auto settings = server_settings();
        if ( ! filters) settings.services = uint64_t(message::version::service::node_network);
        auto peer = std::make_shared<network::peer_session>(std::move(socket), settings, true);
        auto config = network::make_handshake_config(settings, chain().tip(), 0x5eed0000 + sessions.load());
        config.ws_discovery = false;
        auto const handshake = co_await network::perform_handshake_direct(*peer, config);
        if ( ! handshake) {
            peer->stop();
            co_return;
        }
        ::asio::co_spawn(io, [peer]() -> ::asio::awaitable<void> { (void)co_await peer->run(); }, ::asio::detached);
        live.push_back(peer);
        auto const version = peer->negotiated_version();
        // What the mempool holds is announced once the client asks its first
        // question: by then its handshake is over and it reads announcements.
        bool announced = false;
        for (;;) {
            auto received = co_await peer->receive(std::chrono::steady_clock::now() + 120s);
            if ( ! received) break;
            if ( ! announced && ( ! chain().pooled.empty() || ! chain().proofs.empty() || ! chain().pushed.empty())) {
                announced = true;
                // What a peer pushes without announcing it.
                for (auto const& tx : chain().pushed) (void)co_await peer->send(message::transaction(tx));
                message::inventory_vector::list items;
                for (auto const& tx : chain().pooled) items.emplace_back(message::inventory_vector::type_id::transaction, tx.hash());
                for (auto const& proof : chain().proofs) items.emplace_back(message::inventory_vector::type_id::double_spend_proof, proof.hash());
                auto const inventory = message::inventory::create(std::move(items));
                if (inventory) {
                    auto const sent = co_await peer->send(*inventory);
                    if ( ! sent) ++announcements;
                }
            }
            auto const& raw = *received;
            auto const command = raw.heading.command();
            byte_reader reader(raw.payload);
            if (command == message::ping::command) {
                auto const ping = message::ping::from_data(reader, version);
                if (ping) (void)co_await peer->send(message::pong(ping->nonce()));
            } else if (command == message::get_headers::command) {
                auto const ask = message::get_headers::from_data(reader, version);
                if ( ! ask) continue;
                uint32_t from = 0;
                for (auto const& hash : ask->start_hashes()) {
                    if (auto const at = height_of(hash)) {
                        from = *at;
                        break;
                    }
                }
                message::header::list list;
                for (auto height = from + 1; height <= chain().tip() && list.size() < 2000; ++height) {
                    list.emplace_back(chain().blocks[height].header());
                }
                auto const answer = message::headers::create(std::move(list));
                if (answer) (void)co_await peer->send(*answer);
            } else if (command == message::get_compact_filter_checkpoint::command) {
                auto const ask = message::get_compact_filter_checkpoint::from_data(reader, version);
                if ( ! ask || ask->filter_type == 0 || ask->filter_type > 2) continue;
                auto const stop = height_of(ask->stop_hash);
                if ( ! stop) {
                    if ( ! was_silent.exchange(true)) silent.set_value();
                    continue;
                }
                message::compact_filter_checkpoint answer{ask->filter_type, ask->stop_hash, {}};
                for (uint32_t height = 1000; height <= *stop; height += 1000) {
                    answer.filter_headers.push_back(chain().headers[ask->filter_type][height]);
                }
                if (chain().short_checkpoints && ! answer.filter_headers.empty()) answer.filter_headers.pop_back();
                (void)co_await peer->send(answer);
            } else if (command == message::get_compact_filter_headers::command) {
                auto const ask = message::get_compact_filter_headers::from_data(reader, version);
                if ( ! ask || ask->filter_type == 0 || ask->filter_type > 2) continue;
                auto const stop = height_of(ask->stop_hash);
                if ( ! stop || ask->start_height > *stop || *stop - ask->start_height >= 2000) continue;
                auto const type = ask->filter_type;
                message::compact_filter_headers answer{type, ask->stop_hash,
                    ask->start_height == 0 ? null_hash : chain().headers[type][ask->start_height - 1], {}};
                for (auto height = ask->start_height; height <= *stop; ++height) {
                    answer.filter_hashes.push_back(chain().hashes[type][height]);
                }
                (void)co_await peer->send(answer);
            } else if (command == message::get_compact_filters::command) {
                auto const ask = message::get_compact_filters::from_data(reader, version);
                if ( ! ask || ask->filter_type == 0 || ask->filter_type > 2) continue;
                auto const stop = height_of(ask->stop_hash);
                if ( ! stop || ask->start_height > *stop) continue;
                for (auto height = ask->start_height; height <= *stop; ++height) {
                    message::compact_filter answer{ask->filter_type, chain().blocks[height].hash(), chain().filters[ask->filter_type][height]};
                    (void)co_await peer->send(answer);
                }
            } else if (command == message::get_data::command) {
                auto const ask = message::get_data::from_data(reader, version);
                if ( ! ask) continue;
                for (auto const& item : ask->inventories()) {
                    auto const at = height_of(item.hash());
                    if (item.type() == message::inventory_vector::type_id::block && at) {
                        (void)co_await peer->send(message::block(chain().blocks[*at]));
                    }
                    if (item.type() == message::inventory_vector::type_id::transaction) {
                        for (auto const& tx : chain().pooled) {
                            if (tx.hash() != item.hash()) continue;
                            (void)co_await peer->send(message::transaction(tx));
                            ++served_transactions;
                        }
                    }
                    if (item.type() == message::inventory_vector::type_id::double_spend_proof) {
                        for (auto const& proof : chain().proofs) {
                            if (proof.hash() != item.hash()) continue;
                            (void)co_await peer->send(proof);
                            ++served_proofs;
                        }
                    }
                }
            }
        }
        peer->stop();
    }
};

template <typename Awaitable>
auto wait_for(wallet::service& service, Awaitable&& operation) {
    auto future = ::asio::co_spawn(service.executor(), std::forward<Awaitable>(operation), ::asio::use_future);
    REQUIRE(future.wait_for(60s) == std::future_status::ready);
    return future.get();
}

// Whether the service's status comes to satisfy `reached` within 30 s, woken
// by its changes (a moved status is one), not by sleeping between looks.
template <typename Predicate>
bool status_reaches(wallet::service& service, Predicate reached) {
    auto const deadline = std::chrono::steady_clock::now() + 30s;
    auto seen = wait_for(service, service.changed(0, 0s));
    while ( ! reached(service.current())) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        seen = wait_for(service, service.changed(seen, 5s));
    }
    return true;
}

struct running_service {
    std::unique_ptr<wallet::service> service;
    std::future<std::error_code> running;

    explicit running_service(wallet::peer_settings settings) {
        auto made = wallet::service::from_peers(std::move(settings));
        REQUIRE(made);
        service = std::move(*made);
        running = ::asio::co_spawn(service->executor(), service->run(), ::asio::use_future);
    }

    ~running_service() {
        service->stop();
        (void)running.wait_for(30s);
    }
};

// The getdata requests for blocks in a sent log: their heights in `served`.
inline std::set<uint32_t> blocks_asked(std::filesystem::path const& log_path, served_chain const& served) {
    std::set<uint32_t> out;
    std::ifstream log(log_path);
    std::string line;
    while (std::getline(log, line)) {
        if ( ! line.starts_with("getdata ")) continue;
        auto const payload = decode_base16(line.substr(8));
        if ( ! payload) continue;
        byte_reader reader(*payload);
        auto const ask = message::get_data::from_data(reader, message::version::level::maximum);
        if ( ! ask) continue;
        for (auto const& item : ask->inventories()) {
            if (item.type() != message::inventory_vector::type_id::block) continue;
            auto const found = served.heights.find(item.hash());
            if (found != served.heights.end()) out.insert(found->second);
        }
    }
    return out;
}

inline wallet::peer_settings settings_for(std::filesystem::path const& directory, std::vector<std::string> peers) {
    wallet::peer_settings settings;
    settings.network = domain::config::network::regtest;
    settings.directory = directory;
    settings.peers = std::move(peers);
    settings.scan_threads = 2;
    settings.request_timeout = 10s;
    settings.header_capacity = 5000;
    return settings;
}


// Height 5 pays the watched script and mints a token to another script; 9
// spends the watched coin; 12 pays it again; 15 has a transaction no target
// touches; 20 one whose first input a sender ground for RPA. 30 blocks.
struct answers_chain {
    served_chain served;
    hash_digest category = digest_of(7);
    chain::token_data_t nft = chain::make_non_fungible(category, chain::capability_t::minting, data_chunk{0x0a});
    chain::transaction paying = spending({chain::point(digest_of(100), 0)},
        {{1000, watched_script}, {2000, other_script}, {3000, other_script, nft}}, 1);
    chain::transaction spend = spending({chain::point(paying.hash(), 0)}, {{900, other_script}}, 2);
    chain::transaction again = spending({chain::point(digest_of(101), 1)}, {{500, watched_script}}, 3);
    chain::transaction unrelated = spending({chain::point(digest_of(102), 0)}, {{77, other_script}}, 4);
    chain::transaction ground = spending({chain::point(digest_of(103), 0), chain::point(digest_of(104), 0)}, {{66, other_script}}, 5);

    answers_chain() {
        for (uint32_t height = 1; height <= 30; ++height) {
            std::vector<chain::transaction> txs;
            if (height == 5) txs.push_back(paying);
            if (height == 9) txs.push_back(spend);
            if (height == 12) txs.push_back(again);
            if (height == 15) txs.push_back(unrelated);
            if (height == 20) txs.push_back(ground);
            served.mine(std::move(txs));
        }
    }
};

} // namespace

#endif // KTH_NODE_TEST_WALLET_PEER_HARNESS_HPP
