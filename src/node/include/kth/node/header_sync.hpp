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
#include <system_error>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <kth/domain.hpp>
#include <kth/node/define.hpp>

#if defined(KTH_BROWSER_NODE)
namespace kth::node {

// Headers-first actor. All access belongs to its executor, including cancellation and destruction.
// Optional body sampling checks known headers, Merkle commitments and transaction structure,
// then discards full bodies. No UTXO database or full input/script validation is provided.
// A successful run means the selected peers have no more headers, not independent peer agreement.
class KND_API header_sync {
public:
    header_sync(std::string const& bridge_url, domain::config::network network, size_t capacity);
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
    // Refuses an active run without mutation. False also reports inability to
    // prepare the idle status; no C++ exception escapes through the C boundary.
    bool reset() noexcept;
    uint64_t last_height() const;
    // Active-chain header queries. Empty for heights outside the validated active chain.
    // Hex contains the 80-byte wire header; hash uses conventional displayed byte order.
    std::string header_hex(uint64_t height) const;
    std::string header_hash(uint64_t height) const;
    bool running() const;
    // True after this run and the shared Worker executor have drained.
    bool idle() const;
    std::string status() const;
    // Remote version-message user agent for the current session. Empty before handshake
    // and after disconnect/stop. Access belongs to the same executor as the actor.
    std::string peer_user_agent() const;
    size_t index_memory_bytes() const;
    // Snapshot of this actor's effective native settings, not full-node defaults.
    // JSON preserves booleans/numbers, with integers outside JS's safe range as strings.
    std::string runtime_settings() const;
    // Current active tip consensus state, built by the native header validator.
    // Owned JSON snapshot; no borrowed state survives a tip change.
    std::string chain_state_snapshot() const;
    // Configure distinct WS/WSS endpoints while stopped. Bounded to eight sessions.
    // False for duplicate/invalid endpoints, capacity or mutation during a run.
    bool add_peer(std::string const& url);
    // Discovery policy is explicit and immutable while the actor is running.
    // Private endpoints are disabled unless the embedding application opts into a LAN experiment.
    bool configure_discovery(bool enabled, bool allow_private, bool wss_only);
    std::string peer_source(size_t at) const;
    uint64_t peer_announcements(size_t at) const;
    // Optional bounded content sampling, configured while stopped. No UTXO validation.
    bool configure_content(bool blocks, bool transactions);
    std::string block_metadata(uint64_t height, bool include_transactions) const;
    std::string content_state() const;
    std::string observed_transactions() const;
    size_t peer_count() const;
    std::string peer_endpoint(size_t at) const;
    std::string peer_state(size_t at) const;
    std::string peer_agent(size_t at) const;
    std::string peer_error(size_t at) const;
    uint64_t peer_height(size_t at) const;
    bool peer_connected(size_t at) const;

private:
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> run_session(bool follow_tip);
    void spawn_peer(size_t at, bool follow_tip);
    ::asio::awaitable<void> run_peer(size_t at, bool follow_tip);
    struct impl;
    std::unique_ptr<impl> state_;
};
}
#endif // KTH_BROWSER_NODE
#endif
