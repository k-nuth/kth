// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_SAM_HPP
#define KTH_NETWORK_SAM_HPP

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>
#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/handlers.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/network/define.hpp>

namespace kth::network {

struct sam_inbound {
    ::asio::ip::tcp::socket socket;
    infrastructure::config::authority peer;
};

/// A SAM control socket owns the destination used by all its streams.
class KN_API sam_session {
public:
    sam_session(::asio::any_io_executor executor, std::string bridge,
        kth::path private_key_file, bool accept_incoming,
        std::chrono::steady_clock::duration timeout);
    ~sam_session();

    sam_session(sam_session const&) = delete;
    sam_session& operator=(sam_session const&) = delete;

    ::asio::awaitable<void> run();
    void stop();

    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::optional<infrastructure::config::authority> local_address() const;
    [[nodiscard]] awaitable_expected<::asio::ip::tcp::socket> connect(std::string hostname);
    [[nodiscard]] awaitable_expected<sam_inbound> accept();

private:
    struct impl;
    std::shared_ptr<impl> impl_;
};

} // namespace kth::network

#endif // KTH_NETWORK_SAM_HPP
