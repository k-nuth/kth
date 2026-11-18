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
#include <vector>

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

    // With BCH large templates, transaction data goes out in frames of at
    // most this payload, built one at a time as the socket drains. A BCH
    // transaction is at most 1,000,000 bytes, so each frame holds at least one.
    static constexpr size_t data_page_size = 1024 * 1024;

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

    // Retire old work, but remember which issued ids belonged to an old tip.
    // The replaced tip's templates still take solutions until the next tip.
    void set_tip(hash_digest const& previous);

    // The latest CoinbaseOutputConstraints from this client, if any. Templates
    // for this connection must leave this room in the coinbase.
    [[nodiscard]]
    std::optional<coinbase_output_constraints> const& constraints() const {
        return constraints_;
    }

    // A connection that negotiated BCH large templates (0x4C54) is pushed
    // each template's transactions after its NewTemplate and SetNewPrevHash,
    // and is answered RequestTransactionData in pages. That data is taken a
    // frame at a time with next_data_frame() while sending_data().
    [[nodiscard]] bool sending_data() const;

    // An answer the client asked for is still to be sent. The caller reads no
    // further message until it is; any push ahead of it is finished first.
    [[nodiscard]] bool answering() const;

    // The next frame of that data. Empty when the template went away before
    // its push finished.
    [[nodiscard]] data_chunk next_data_frame();

private:
    enum class state { awaiting_setup, ready, closed };

    result close();

    struct entry {
        uint64_t id;
        blockchain::mining_template value;
        std::vector<hash_digest> merkle_path;
        // Its transactions do not fit RequestTransactionData.Success.
        bool too_large;
        bool refusal_logged;
    };

    // A current template, for any request. A solution may also name one of
    // the replaced tip's.
    [[nodiscard]] entry* find_template(uint64_t id);
    [[nodiscard]] entry const* find_solvable(uint64_t id) const;
    [[nodiscard]] bool stale(uint64_t id) const;

    // One template's transactions on their way: a push, or the answer to a
    // request, which leaves the push base alone.
    struct transfer {
        uint64_t id;
        bool push;
        // A push's transaction ids in block order, the next base once sent.
        std::vector<hash_digest> txids;
        // A delta sends Kept, then only what the base lacks.
        bool delta = false;
        bool kept_sent = false;
        size_t next = 0;        // The template's next transaction.
        size_t base_next = 0;   // The base's next id, for a delta.
    };

    void push(uint64_t id, std::vector<hash_digest> txids);
    void abandon();
    void skip_kept();
    [[nodiscard]] data_chunk kept_frame() const;
    [[nodiscard]] data_chunk page_frame(std::vector<transaction_const_ptr> const& txs);

    state state_ = state::awaiting_setup;
    std::optional<coinbase_output_constraints> constraints_;
    std::optional<hash_digest> previous_;
    std::deque<entry> templates_;
    // The templates of the tip just replaced, at most max_templates.
    std::deque<entry> replaced_;
    uint64_t next_id_ = 1;
    uint64_t stale_through_ = 0;

    bool large_templates_ = false;
    std::optional<transfer> transfer_;
    // At most one answer waits: the listener reads nothing until it is sent.
    std::optional<uint64_t> requested_;
    // The last template whose push finished, which the next push names as its
    // base, and its transaction ids in block order. The client tracks it too.
    uint64_t base_id_ = 0;
    std::vector<hash_digest> base_txids_;
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
