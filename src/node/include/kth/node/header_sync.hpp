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
    // Requests cancellation only. The pending run() must settle before disposal or reset().
    // Logically const cancellation, like a stop source: no session/index replacement.
    ::asio::awaitable<std::expected<uint64_t, std::error_code>> request_stop() const;
    void reset();
    uint64_t last_height() const;
    bool running() const;
    // True after this run and the shared Worker executor have drained.
    bool idle() const;
    std::string status() const;
    size_t index_memory_bytes() const;

private:
    struct impl;
    std::unique_ptr<impl> state_;
};
}
#endif // KTH_BROWSER_NODE
#endif
