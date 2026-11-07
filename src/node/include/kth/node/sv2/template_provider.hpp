// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_TEMPLATE_PROVIDER_HPP
#define KTH_NODE_SV2_TEMPLATE_PROVIDER_HPP

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <system_error>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/strand.hpp>
#include <asio/experimental/concurrent_channel.hpp>

#include <kth/infrastructure/utility/data.hpp>
#include <kth/blockchain/pools/block_template.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/node/settings.hpp>
#include <kth/node/sv2/framing.hpp>
#include <kth/node/sv2/messages.hpp>
#include <kth/node/sv2/noise_credentials.hpp>

// Stratum V2 Template Provider and Mining Protocol listener (sv2-spec 05, 07):
// connection setup and block template flow, over the authenticated
// Noise transport (sv2-spec 04) unless explicitly disabled by the operator.

namespace kth::blockchain { struct block_chain; }

namespace kth::node::sv2 {

// The protocol state of one connection, kept apart from the socket so its
// rules can be tested without one. process() takes one complete frame and
// says what to send back and whether to close the connection.
struct template_provider_session {
    // The largest payload read from a client. The client's own messages are
    // small (SubmitSolution, the largest, carries a coinbase transaction of at
    // most 64 KiB); the rest of the room is for extension messages, which are
    // read and discarded. A longer frame closes the connection unread.
    static constexpr uint32_t max_payload_size = 1024 * 1024;
    static constexpr size_t max_templates = 8;

    // Include the header and the widest CompactSize counts as well as the
    // fixed coinbase fields and a full 100-byte scriptSig. Outputs are extra.
    static constexpr uint64_t coinbase_fixed_reserve = 248;

    struct result {
        data_chunk response;    // A complete frame to send, or empty.
        bool close = false;     // Close the connection after the response.
        bool constraints_changed = false;
        block_const_ptr solution;
    };

    [[nodiscard]]
    result process(message_header const& header, byte_span payload);

    // Publish a chain-produced selection, with NewTemplate before SetNewPrevHash.
    // The caller fetches it using this connection's byte and SigChecks reserves.
    [[nodiscard]]
    result set_template(blockchain::mining_template value);

    // Discard old work, but remember which issued ids belonged to an old tip.
    void set_tip(hash_digest const& previous);

    // The latest CoinbaseOutputConstraints from this client, if any. Templates
    // for this connection must leave this room in the coinbase.
    [[nodiscard]]
    std::optional<coinbase_output_constraints> const& constraints() const {
        return constraints_;
    }

private:
    enum class state { awaiting_setup, ready, closed };

    result close();

    struct entry {
        uint64_t id;
        blockchain::mining_template value;
        std::vector<hash_digest> merkle_path;
    };

    [[nodiscard]] entry const* find_template(uint64_t id) const;
    [[nodiscard]] bool stale(uint64_t id) const;

    state state_ = state::awaiting_setup;
    std::optional<coinbase_output_constraints> constraints_;
    std::optional<hash_digest> previous_;
    std::deque<entry> templates_;
    uint64_t next_id_ = 1;
    uint64_t stale_through_ = 0;
};

struct jd_client;

// Accepts Template Distribution and Mining Protocol clients on sv2.bind:sv2.port.
// Like the JSON-RPC server it runs on the node's executor, with one coroutine
// per connection, and every socket is owned by one strand.
struct template_provider {
    template_provider(sv2_settings const& settings, ::asio::any_io_executor executor,
        blockchain::block_chain& chain,
        domain::config::network network = domain::config::network::mainnet);

    // Bind the listener and serve connections until stop(). Intended to be
    // co_spawned. Returns on a bind error, or once every connection has ended
    // after stop().
    ::asio::awaitable<void> run();

    // A bounded stop message, including when run() has not started yet.
    // Socket cancellation itself is performed only by the owner strand.
    void stop();

    // One startup outcome: the actual bound port, or the listener's error.
    // May be awaited before run() starts. Cancellation wakes the waiter.
    [[nodiscard]] ::asio::awaitable<uint16_t> wait_until_listening();

private:
    ::asio::awaitable<void> run_owned();
    ::asio::awaitable<void> serve_connection(::asio::ip::tcp::socket& socket,
        jd_client* upstream);

    sv2_settings const settings_;
    domain::config::network const network_;
    blockchain::block_chain& chain_;
    std::optional<domain::chain::script> payout_;
    uint64_t next_salt_ = 1;
    std::shared_ptr<uint64_t> pool_allocations_ = std::make_shared<uint64_t>(0);
    ::asio::strand<::asio::any_io_executor> owner_;
    ::asio::experimental::concurrent_channel<void(std::error_code, int)> stop_requests_;
    ::asio::experimental::concurrent_channel<void(std::error_code, uint16_t)> listening_;
    std::optional<noise_credentials> credentials_;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_TEMPLATE_PROVIDER_HPP
