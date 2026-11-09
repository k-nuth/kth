// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef KTH_NODE_HEADER_SYNC_HPP
#define KTH_NODE_HEADER_SYNC_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <system_error>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <kth/domain.hpp>
#include <kth/node/define.hpp>

#if defined(KTH_BROWSER_NODE)
namespace kth::node {

// Headers-only actor. All access belongs to its executor, including cancellation and destruction.
// No block or UTXO database is opened. It synchronizes from up to eight WebSocket peers at once into
// one validated header index: the one given at construction, those added with add_peer(), and those
// learned through endpoint discovery. A successful run means some peer has no more headers, not that
// independent peers agree or that block bodies/transactions were validated.
class KND_API header_sync {
public:
    // operation_failed when the bridge URL is empty or the capacity is
    // outside 1..2000000 headers.
    static std::expected<std::unique_ptr<header_sync>, std::error_code> create(std::string const& bridge_url,
        domain::config::network network, size_t capacity);
    ~header_sync();
    header_sync(header_sync const&) = delete;
    header_sync& operator=(header_sync const&) = delete;

    // In a Worker this schedules cooperative executor progress without blocking the browser event loop.
    ::asio::any_io_executor executor() const;
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> run();
    // Keep the session alive at the tip; returns only after stop or a connection/protocol error.
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> follow();
    // Requests cancellation only. The pending run()/follow() must settle before disposal or reset().
    // Logically const cancellation, like a stop source: no session/index replacement.
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> request_stop() const;
    // Coalesced refresh request for an active session; does not replace or close its peer.
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> request_refresh() const;
    // Allows a new run after a stop, and retries peers rejected for invalid
    // data. operation_failed until settled().
    std::error_code reset();

    // An operation accepted for this client before it runs, as when it is
    // queued on the executor: held from acceptance until its completion has
    // run, so the client is not reset or disposed of under it.
    class admission {
    public:
        admission(admission&& other) noexcept : count_(std::exchange(other.count_, nullptr)) {}
        admission& operator=(admission&&) = delete;
        admission(admission const&) = delete;
        ~admission() { if (count_) --*count_; }
    private:
        friend class header_sync;
        explicit admission(size_t& count) : count_(&count) { ++count; }
        size_t* count_;
    };
    admission admit() const;
    // No run(), follow() or admitted operation is pending: the client may be
    // reset or disposed of.
    bool settled() const;
    uint64_t last_height() const;
    // Active-chain header queries. Empty for heights outside the validated active chain.
    // Hex contains the 80-byte wire header; hash uses conventional displayed byte order.
    std::string header_hex(uint64_t height) const;
    std::string header_hash(uint64_t height) const;
    bool running() const;
    // True after this run and the shared Worker executor have drained.
    bool idle() const;
    std::string status() const;
    size_t index_memory_bytes() const;

    // JSON snapshots, owned by the caller; integers past JavaScript's safe
    // range are strings. The consensus state of the active tip, built by the
    // header validator ("null" at genesis); and this client's effective
    // settings (node, network, chain and header_sync sections).
    std::string chain_state_snapshot() const;
    std::string runtime_settings() const;

    // Peers, configured while settled. A distinct ws:// or wss:// URL, up to
    // eight in all; false otherwise.
    bool add_peer(std::string const& url);
    // Endpoint discovery (/wsdisc:1/, getwsaddr/wsaddr): whether to negotiate
    // it, accept private endpoints (a LAN experiment), and only WSS ones.
    // Configured while settled; false otherwise. Enabled by default.
    bool configure_discovery(bool enabled, bool allow_private, bool wss_only);

    // Content sampling, configured while settled; off by default. No UTXO or
    // script validation: bodies are checked against their headers and kept as
    // metadata, never whole.
    // - blocks: the bodies of the most recent 100 active blocks are fetched
    //   once caught up, checked (Merkle root, transaction structure) and kept
    //   as metadata, coinbase with its Merkle proof and, within a 16 MiB
    //   budget, transaction ids. Messages up to 32 MiB are then accepted.
    // - transactions: the version message asks for relay, announced
    //   transactions are fetched, checked for structure and kept, at most 1000
    //   and 8 MiB, for 20 minutes or until a sampled block confirms them.
    bool configure_content(bool blocks, bool transactions);
    // JSON. A sampled block ("null" if not sampled or no longer active), with
    // its coinbase, proof and transaction ids when asked; the sampling state;
    // and the observed transactions (txid, size, time received).
    std::string block_metadata(uint64_t height, bool include_transactions) const;
    std::string content_state() const;
    std::string observed_transactions() const;

    // Per peer, by position (0 is the constructor's): empty or zero past the end.
    size_t peer_count() const;
    std::string peer_endpoint(size_t at) const;
    // "configured", or the endpoint of the peer that advertised it.
    std::string peer_source(size_t at) const;
    // idle, connecting, synchronizing, caught_up, retry, disconnected,
    // rejected (it sent invalid data: not retried until reset()) or stopped.
    std::string peer_state(size_t at) const;
    // The user agent of its current session; empty when not connected.
    std::string peer_agent(size_t at) const;
    std::string peer_error(size_t at) const;
    // The height of the last header it served on the validated chain.
    uint64_t peer_height(size_t at) const;
    uint64_t peer_announcements(size_t at) const;
    bool peer_connected(size_t at) const;

private:
    header_sync(std::string const& bridge_url, domain::config::network network, size_t capacity);

    ::asio::awaitable<std::expected<uint64_t, std::error_code>> run_session(bool follow_tip);
    void spawn_peer(size_t at, bool follow_tip);
    ::asio::awaitable<void> run_peer(size_t at, bool follow_tip);
    struct impl;
    std::unique_ptr<impl> state_;
};
}
#endif // KTH_BROWSER_NODE
#endif
