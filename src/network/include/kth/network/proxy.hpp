// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROXY_HPP
#define KTH_NETWORK_PROXY_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <kth/domain.hpp>
#include <kth/network/define.hpp>
#include <kth/network/message_subscriber.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

/// Manages all socket communication, thread safe.
class KN_API proxy
    : public enable_shared_from_base<proxy>, noncopyable
{
public:
    using ptr = std::shared_ptr<proxy>;
    using result_handler = std::function<void(code const&)>;
    using stop_subscriber = subscriber<code>;

    /// Construct an instance.
    proxy(threadpool& pool, socket::ptr socket, settings const& settings);

    /// Validate proxy stopped.
    ~proxy();

    /// Send a message on the socket.
    template <typename Message>
    void send(Message const& message, result_handler handler) {
        auto data = domain::message::serialize(version_, message, protocol_magic_);
        auto const payload = std::make_shared<data_chunk>(std::move(data));
        auto const command = std::make_shared<std::string>(message.command);

        // Sequential dispatch is required because write may occur in multiple
        // asynchronous steps invoked on different threads, causing deadlocks.
        dispatch_.lock(&proxy::do_send, shared_from_this(), command, payload, handler);
    }

    /// Subscribe to messages of the specified type on the socket.
    template <typename Message>
    void subscribe(message_handler<Message>&& handler) {
        message_subscriber_.subscribe<Message>(std::forward<message_handler<Message>>(handler));
    }

    /// Subscribe to the stop event.
    virtual
    void subscribe_stop(result_handler handler);

    /// Get the authority of the far end of this socket.
    virtual
    const infrastructure::config::authority& authority() const;

    /// Get the negotiated protocol version of this socket.
    /// The value should be the lesser of own max and peer min.
    uint32_t negotiated_version() const;

    /// Save the negotiated protocol version.
    virtual
    void set_negotiated_version(uint32_t value);

    /// Read messages from this socket.
    virtual
    void start(result_handler handler);

    /// Stop reading or sending messages on this socket.
    virtual
    void stop(code const& ec);

protected:
    virtual
    bool stopped() const;

    virtual
    void signal_activity() = 0;

    virtual
    void handle_stopping() = 0;

private:
    using command_ptr = std::shared_ptr<std::string>;
    using payload_ptr = std::shared_ptr<data_chunk>;

    static infrastructure::config::authority authority_factory(socket::ptr socket);

    void do_close();

    void read_heading();
    void handle_read_heading(boost_code const& ec, size_t payload_size);

    void read_payload(const domain::message::heading& head);
    void handle_read_payload(boost_code const& ec, size_t, const domain::message::heading& head);

    void do_send(command_ptr command, payload_ptr payload, result_handler handler);
    void handle_send(boost_code const& ec, size_t bytes, command_ptr command, payload_ptr payload, result_handler handler);

    infrastructure::config::authority const authority_;

    // These are protected by read header/payload ordering.
    data_chunk heading_buffer_;
    data_chunk payload_buffer_;
    socket::ptr socket_;

    // These are thread safe.
    std::atomic<bool> stopped_;
    uint32_t const protocol_magic_;
    size_t const maximum_payload_;
    bool const validate_checksum_;
    bool const verbose_;
    std::atomic<uint32_t> version_;
    message_subscriber message_subscriber_;
    stop_subscriber::ptr stop_subscriber_;
    dispatcher dispatch_;
};

} // namespace kth::network

#endif

