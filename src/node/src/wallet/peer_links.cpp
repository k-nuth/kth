// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers: the connections, the requests, and
// what peers send without being asked. Only what was asked of a peer is
// taken from it: a transaction or a double-spend proof it pushes unasked is
// dropped.

#include "peer_source.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/network/protocols_coro.hpp>

namespace kth::node::wallet::detail {

using network::raw_message;

namespace {

// Requests remembered at most, before they are forgotten all at once.
constexpr size_t requested_limit = 100'000;
constexpr size_t requested_dsproof_limit = 10'000;

#if ! defined(KTH_BROWSER_NODE)
// host:port, [v6]:port or host alone.
std::pair<std::string, uint16_t> split_endpoint(std::string const& endpoint, uint16_t fallback) {
    auto host = endpoint;
    uint16_t port = fallback;
    auto const parse_port = [&](std::string const& text) {
        if (text.empty() || ! std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; }) || text.size() > 5) return;
        auto const value = std::stoul(text);
        if (value > 0 && value <= 65535) port = uint16_t(value);
    };
    if ( ! host.empty() && host.front() == '[') {
        auto const close = host.find(']');
        if (close != std::string::npos) {
            if (close + 1 < host.size() && host[close + 1] == ':') parse_port(host.substr(close + 2));
            host = host.substr(1, close - 1);
        }
        return {host, port};
    }
    auto const colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon) {
        parse_port(host.substr(colon + 1));
        host = host.substr(0, colon);
    }
    return {host, port};
}
#endif

} // namespace

void peer_impl::log_sent(std::string_view command, data_chunk const& packet) {
    bytes_sent += packet.size();
    if ( ! sent_log.is_open()) return;
    auto const payload = packet.size() > message::heading::satoshi_fixed_size()
        ? byte_span(packet.data() + message::heading::satoshi_fixed_size(), packet.size() - message::heading::satoshi_fixed_size())
        : byte_span{};
    sent_log << command << ' ' << encode_base16(payload) << '\n';
    sent_log.flush();
}

::asio::awaitable<void> peer_impl::pump(std::shared_ptr<peer_link> link) {
    while ( ! stopping) {
        auto received = co_await link->session->receive();
        if ( ! received) break;
        auto raw = std::move(*received);
        bytes_received += message::heading::satoshi_fixed_size() + raw.payload.size();
        bool taken = false;
        for (auto it = link->waiters.begin(); it != link->waiters.end(); ++it) {
            auto* entry = *it;
            auto const next = entry->match(raw);
            if (next == step::skip) continue;
            entry->taken.push_back(std::move(raw));
            if (next == step::done) {
                link->waiters.erase(it);
                (void)entry->signal.try_send(0);
            }
            taken = true;
            break;
        }
        if ( ! taken) co_await unsolicited(link, raw);
    }
    link->closed = true;
    for (auto* entry : link->waiters) entry->signal.close();
    link->waiters.clear();
    // What a closed peer said about fees says nothing any more, and what it
    // was asked for and did not send is asked of the next peer to announce it.
    fee_filters.erase(link->id);
    std::erase_if(requested, [&](auto const& entry) { return entry.second == link->id; });
    std::erase_if(requested_dsproofs, [&](auto const& entry) { return entry.second == link->id; });
    update_status();
}

::asio::awaitable<void> peer_impl::unsolicited(std::shared_ptr<peer_link> link, raw_message const& raw) {
    auto const command = raw.heading.command();
    byte_reader reader(raw.payload);
    auto const version = link->session->negotiated_version();
    if (command == message::ping::command) {
        auto const ping = message::ping::from_data(reader, version);
        if (ping) (void)co_await send(*link, message::pong(ping->nonce()));
    } else if (command == message::reject::command) {
        auto const rejected = message::reject::from_data(reader, version);
        if (rejected && pending_broadcasts.contains(rejected->data())) {
            rejections[rejected->data()] = rejected->reason();
        }
    } else if (link->isolated) {
        // A connection of its own serves one block or one broadcast and
        // closes: what it announces is fetched over the long-lived ones.
        co_return;
    } else if (command == message::inventory::command) {
        auto const inventory = message::inventory::from_data(reader, version);
        if ( ! inventory) co_return;
        hash_list wanted;
        hash_list proofs;
        for (auto const& item : inventory->inventories()) {
            if (item.type() == type_id::block) {
                ring();
            } else if (item.type() == type_id::transaction) {
                // Every announced transaction is fetched: what is asked for says nothing.
                if ( ! pool.contains(item.hash()) && requested.emplace(item.hash(), link->id).second) wanted.push_back(item.hash());
            } else if (item.type() == type_id::double_spend_proof) {
                if ( ! dsproofs.contains(item.hash()) && requested_dsproofs.emplace(item.hash(), link->id).second) {
                    proofs.push_back(item.hash());
                }
            }
        }
        // Transactions first: a proof is about one of them.
        if (requested.size() > requested_limit) requested.clear();
        if ( ! wanted.empty()) {
            auto const fetch = message::get_data::create(wanted, type_id::transaction);
            if (fetch) (void)co_await send(*link, *fetch);
        }
        if (requested_dsproofs.size() > requested_dsproof_limit) requested_dsproofs.clear();
        if ( ! proofs.empty()) {
            auto const fetch = message::get_data::create(proofs, type_id::double_spend_proof);
            if (fetch) (void)co_await send(*link, *fetch);
        }
    } else if (command == message::headers::command) {
        ring();
    } else if (command == message::transaction::command) {
        auto tx = message::transaction::from_data(reader, version);
        if ( ! tx) co_return;
        // Only what this peer was asked for: a transaction pushed unasked
        // could show a payment nobody announced.
        auto const asked = requested.find(tx->hash());
        if (asked == requested.end() || asked->second != link->id) co_return;
        requested.erase(asked);
        on_mempool_tx(domain::chain::transaction(std::move(*tx)));
    } else if (command == message::double_spend_proof::command) {
        auto const proof = message::double_spend_proof::from_data(reader, version);
        if ( ! proof) co_return;
        auto const asked = requested_dsproofs.find(proof->hash());
        if (asked == requested_dsproofs.end() || asked->second != link->id) co_return;
        requested_dsproofs.erase(asked);
        on_dsproof(*proof, raw.payload);
    } else if (command == message::fee_filter::command) {
        auto const fee = message::fee_filter::from_data(reader, version);
        if (fee) fee_filters[link->id] = fee->minimum_fee();
    }
}

uint64_t peer_impl::next_nonce() {
    uint64_t nonce = 0;
    while (nonce == 0) nonce = random();
    return nonce;
}

::asio::awaitable<std::shared_ptr<peer_link>> peer_impl::connect(std::string endpoint, bool isolated) {
    auto executor = co_await ::asio::this_coro::executor;
#if defined(KTH_BROWSER_NODE)
    // A ws:// or wss:// bridge that relays the peer's byte stream.
    auto bridged = transport;
    bridged.browser_bridge_url = endpoint;
    auto connected = co_await network::async_connect(executor, "ws-peer", 8333, bridged, connect_timeout);
#else
    auto const [host, port] = split_endpoint(endpoint, transport.inbound_port);
    auto connected = co_await network::async_connect(executor, host, port, transport, connect_timeout);
#endif
    if ( ! connected) co_return nullptr;
    auto session = *connected;
    auto config = network::make_handshake_config(transport, header_tip(), next_nonce());
    config.ws_discovery = false;
    auto handshake = co_await network::perform_handshake_direct(*session, config);
    if ( ! handshake) {
        session->stop();
        co_return nullptr;
    }
    auto link = std::make_shared<peer_link>();
    link->id = next_link_id++;
    link->endpoint = std::move(endpoint);
    link->session = session;
    link->isolated = isolated;
    if (handshake->peer_version) link->services = handshake->peer_version->services();
    tasks->spawn("wallet peer I/O", [session]() -> ::asio::awaitable<void> { (void)co_await session->run(); });
    tasks->spawn("wallet peer reader", pump(link));
    // What a long-lived peer's mempool held before it was connected is
    // announced on request (a `mempool` message names nothing), by a peer
    // that serves it: one that offers bloom filters, as BCHN asks.
    if ( ! isolated && (link->services & uint64_t(message::version::service::node_bloom)) != 0) {
        (void)co_await send(*link, message::memory_pool{});
    }
    co_return link;
}

// A connection of its own to the next peer that is not refused: what is
// asked over it is not tied to the long-lived connections.
::asio::awaitable<std::shared_ptr<peer_link>> peer_impl::connect_isolated() {
    for (size_t attempt = 0; attempt < endpoints.size() && ! stopping; ++attempt) {
        auto const endpoint = endpoints[next_endpoint++ % endpoints.size()];
        if (refused_endpoints.contains(endpoint)) continue;
        auto link = co_await connect(endpoint, true);
        if (link) co_return link;
    }
    co_return nullptr;
}

::asio::awaitable<void> peer_impl::keep_peers() {
    std::erase_if(links, [](auto const& link) { return link->closed; });
    auto const usable = size_t(std::ranges::count_if(endpoints, [this](auto const& endpoint) {
        return ! refused_endpoints.contains(endpoint);
    }));
    auto const wanted = std::min(settings.service_peers, usable);
    size_t attempts = 0;
    while (links.size() < wanted && attempts < endpoints.size() && ! stopping) {
        auto const endpoint = endpoints[next_endpoint++ % endpoints.size()];
        ++attempts;
        if (refused_endpoints.contains(endpoint)) continue;
        if (std::ranges::any_of(links, [&](auto const& link) { return link->endpoint == endpoint; })) continue;
        auto link = co_await connect(endpoint, false);
        if (link) links.push_back(std::move(link));
    }
    update_status();
}

std::shared_ptr<peer_link> peer_impl::next_link() {
    std::vector<std::shared_ptr<peer_link>> open;
    for (auto const& link : links) {
        if ( ! link->closed) open.push_back(link);
    }
    if (open.empty()) return nullptr;
    return open[next_link_at++ % open.size()];
}

std::vector<std::shared_ptr<peer_link>> peer_impl::filter_servers(uint8_t type) const {
#if defined(KTH_CURRENCY_BCH)
    auto const filters = uint64_t(message::version::service::node_compact_filters);
#else
    auto const filters = uint64_t{0};
#endif
    std::vector<std::shared_ptr<peer_link>> out;
    for (auto const& link : links) {
        if ( ! link->closed && (link->services & filters) != 0 && ! link->no_filters.contains(type)) out.push_back(link);
    }
    return out;
}

void peer_impl::refuse(peer_link& link, std::string const& why) {
    if (refused_endpoints.contains(link.endpoint) && link.closed) return;
    spdlog::warn("[wallet] Peer {} refused: {}", link.endpoint, why);
    refused.push_back(link.endpoint + ": " + why);
    while (refused.size() > refused_limit) refused.pop_front();
    refused_endpoints.insert(link.endpoint);
    link.no_filters.insert(script_type);
    link.no_filters.insert(prefix_type);
    link.closed = true;
    if (link.session) link.session->stop();
    // Another round, with the peers that are left.
    ring();
}

} // namespace kth::node::wallet::detail
