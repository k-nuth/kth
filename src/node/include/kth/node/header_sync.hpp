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
// No block or UTXO database is opened. A successful run means the selected peer has no more headers,
// not that independent peers agree or that block bodies/transactions were validated.
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
    // Allows a new run after a stop. operation_failed until settled().
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

private:
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> run_session(bool follow_tip);
    struct impl;
    std::unique_ptr<impl> state_;
};
}
#endif // KTH_BROWSER_NODE
#endif
