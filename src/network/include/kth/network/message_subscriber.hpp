// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_MESSAGE_SUBSCRIBER_HPP
#define KTH_NETWORK_MESSAGE_SUBSCRIBER_HPP

#include <istream>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <string>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>

#include <kth/network/define.hpp>
namespace kth::network {

#define DEFINE_SUBSCRIBER_TYPE(value) \
    using value##_subscriber_type = resubscriber<code, domain::message::value::const_ptr>


#define DEFINE_SUBSCRIBER_OVERLOAD(value) \
    template <typename Handler> \
    void subscribe(domain::message::value&&, Handler&& handler) { \
        value##_subscriber_->subscribe(std::forward<Handler>(handler), \
            error::channel_stopped, {}); \
    }

#define DECLARE_SUBSCRIBER(value) \
    value##_subscriber_type::ptr value##_subscriber_

template <typename Message>
using message_handler = std::function<bool(code const&, std::shared_ptr<const Message>)>;

/// Aggregation of subscribers by messasge type, thread safe.
class KN_API message_subscriber : noncopyable {
public:
    DEFINE_SUBSCRIBER_TYPE(address);
    DEFINE_SUBSCRIBER_TYPE(alert);
    DEFINE_SUBSCRIBER_TYPE(block);
    DEFINE_SUBSCRIBER_TYPE(block_transactions);
    DEFINE_SUBSCRIBER_TYPE(compact_block);
    DEFINE_SUBSCRIBER_TYPE(double_spend_proof);
    DEFINE_SUBSCRIBER_TYPE(fee_filter);
    DEFINE_SUBSCRIBER_TYPE(filter_add);
    DEFINE_SUBSCRIBER_TYPE(filter_clear);
    DEFINE_SUBSCRIBER_TYPE(filter_load);
    DEFINE_SUBSCRIBER_TYPE(get_address);
    DEFINE_SUBSCRIBER_TYPE(get_blocks);
    DEFINE_SUBSCRIBER_TYPE(get_block_transactions);
    DEFINE_SUBSCRIBER_TYPE(get_data);
    DEFINE_SUBSCRIBER_TYPE(get_headers);
    DEFINE_SUBSCRIBER_TYPE(headers);
    DEFINE_SUBSCRIBER_TYPE(inventory);
    DEFINE_SUBSCRIBER_TYPE(memory_pool);
    DEFINE_SUBSCRIBER_TYPE(merkle_block);
    DEFINE_SUBSCRIBER_TYPE(not_found);
    DEFINE_SUBSCRIBER_TYPE(ping);
    DEFINE_SUBSCRIBER_TYPE(pong);
    DEFINE_SUBSCRIBER_TYPE(reject);
    DEFINE_SUBSCRIBER_TYPE(send_compact);
    DEFINE_SUBSCRIBER_TYPE(send_headers);
    DEFINE_SUBSCRIBER_TYPE(transaction);
    DEFINE_SUBSCRIBER_TYPE(verack);
    DEFINE_SUBSCRIBER_TYPE(version);
    DEFINE_SUBSCRIBER_TYPE(xversion);
    // DEFINE_SUBSCRIBER_TYPE(xverack);

    /**
     * Create an instance of this class.
     * @param[in]  pool  The threadpool to use for sending notifications.
     */
    message_subscriber(threadpool& pool);

    /**
     * Subscribe to receive a notification when a message of type is received.
     * The handler is unregistered when the call is made.
     * Subscribing must be immediate, we cannot switch thread contexts.
     * @param[in]  handler  The handler to register.
     */
    template <typename Message, typename Handler>
    void subscribe(Handler&& handler) {
        subscribe(Message(), std::forward<Handler>(handler));
    }

    /**
     * Load bytes into a message instance and notify subscribers.
     * @param[in]  reader      The byte reader from which to load the message.
     * @param[in]  version     The peer protocol version.
     * @param[in]  subscriber  The subscriber for the message type.
     * @return                 Returns error::bad_stream if failed.
     */
    template <typename Message, typename Subscriber>
    code relay(byte_reader& reader, uint32_t version, Subscriber& subscriber) const {
        // Subscribers are invoked only with stop and success codes.
        auto msg = Message::from_data(reader, version);
        if ( ! msg) {
            return error::bad_stream;
        }
        auto const msg_ptr = std::make_shared<Message>(std::move(*msg));

        subscriber->relay(error::success, msg_ptr);
        return error::success;
    }

    /**
     * Load bytes into a message instance and invoke subscribers.
     * @param[in]  reader      The byte reader from which to load the message.
     * @param[in]  version     The peer protocol version.
     * @param[in]  subscriber  The subscriber for the message type.
     * @return                 Returns error::bad_stream if failed.
     */
    template <typename Message, typename Subscriber>
    code handle(byte_reader& reader, uint32_t version, Subscriber& subscriber) const {
        // Subscribers are invoked only with stop and success codes.
        auto msg = Message::from_data(reader, version);
        if ( ! msg) {
            return error::bad_stream;
        }
        auto const msg_ptr = std::make_shared<Message>(std::move(*msg));

        subscriber->invoke(error::success, msg_ptr);
        return error::success;
    }


    /**
     * Broadcast a default message instance with the specified error code.
     * @param[in]  ec  The error code to broadcast.
     */
    virtual void broadcast(code const& ec);

    /*
     * Load bytes of the specified command type.
     * Creates an instance of the indicated message type.
     * Sends the message instance to each subscriber of the type.
     * @param[in]  type     The stream message type identifier.
     * @param[in]  version  The peer protocol version.
     * @param[in]  reader   The byte reader from which to load the message.
     * @return              Returns error::bad_stream if failed.
     */
    virtual code load(domain::message::message_type type, uint32_t version, byte_reader& reader) const;

    /**
     * Start all subscribers so that they accept subscription.
     */
    virtual void start();

    /**
     * Stop all subscribers so that they no longer accept subscription.
     */
    virtual void stop();

private:
    DEFINE_SUBSCRIBER_OVERLOAD(address);
    DEFINE_SUBSCRIBER_OVERLOAD(alert);
    DEFINE_SUBSCRIBER_OVERLOAD(block);
    DEFINE_SUBSCRIBER_OVERLOAD(block_transactions);
    DEFINE_SUBSCRIBER_OVERLOAD(compact_block);
    DEFINE_SUBSCRIBER_OVERLOAD(double_spend_proof);
    DEFINE_SUBSCRIBER_OVERLOAD(fee_filter);
    DEFINE_SUBSCRIBER_OVERLOAD(filter_add);
    DEFINE_SUBSCRIBER_OVERLOAD(filter_clear);
    DEFINE_SUBSCRIBER_OVERLOAD(filter_load);
    DEFINE_SUBSCRIBER_OVERLOAD(get_address);
    DEFINE_SUBSCRIBER_OVERLOAD(get_blocks);
    DEFINE_SUBSCRIBER_OVERLOAD(get_block_transactions);
    DEFINE_SUBSCRIBER_OVERLOAD(get_data);
    DEFINE_SUBSCRIBER_OVERLOAD(get_headers);
    DEFINE_SUBSCRIBER_OVERLOAD(headers);
    DEFINE_SUBSCRIBER_OVERLOAD(inventory);
    DEFINE_SUBSCRIBER_OVERLOAD(memory_pool);
    DEFINE_SUBSCRIBER_OVERLOAD(merkle_block);
    DEFINE_SUBSCRIBER_OVERLOAD(not_found);
    DEFINE_SUBSCRIBER_OVERLOAD(ping);
    DEFINE_SUBSCRIBER_OVERLOAD(pong);
    DEFINE_SUBSCRIBER_OVERLOAD(reject);
    DEFINE_SUBSCRIBER_OVERLOAD(send_compact);
    DEFINE_SUBSCRIBER_OVERLOAD(send_headers);
    DEFINE_SUBSCRIBER_OVERLOAD(transaction);
    DEFINE_SUBSCRIBER_OVERLOAD(verack);
    DEFINE_SUBSCRIBER_OVERLOAD(version);
    DEFINE_SUBSCRIBER_OVERLOAD(xversion);
    // DEFINE_SUBSCRIBER_OVERLOAD(xverack);

    DECLARE_SUBSCRIBER(address);
    DECLARE_SUBSCRIBER(alert);
    DECLARE_SUBSCRIBER(block);
    DECLARE_SUBSCRIBER(block_transactions);
    DECLARE_SUBSCRIBER(compact_block);
    DECLARE_SUBSCRIBER(double_spend_proof);
    DECLARE_SUBSCRIBER(fee_filter);
    DECLARE_SUBSCRIBER(filter_add);
    DECLARE_SUBSCRIBER(filter_clear);
    DECLARE_SUBSCRIBER(filter_load);
    DECLARE_SUBSCRIBER(get_address);
    DECLARE_SUBSCRIBER(get_blocks);
    DECLARE_SUBSCRIBER(get_block_transactions);
    DECLARE_SUBSCRIBER(get_data);
    DECLARE_SUBSCRIBER(get_headers);
    DECLARE_SUBSCRIBER(headers);
    DECLARE_SUBSCRIBER(inventory);
    DECLARE_SUBSCRIBER(memory_pool);
    DECLARE_SUBSCRIBER(merkle_block);
    DECLARE_SUBSCRIBER(not_found);
    DECLARE_SUBSCRIBER(ping);
    DECLARE_SUBSCRIBER(pong);
    DECLARE_SUBSCRIBER(reject);
    DECLARE_SUBSCRIBER(send_compact);
    DECLARE_SUBSCRIBER(send_headers);
    DECLARE_SUBSCRIBER(transaction);
    DECLARE_SUBSCRIBER(verack);
    DECLARE_SUBSCRIBER(version);
    DECLARE_SUBSCRIBER(xversion);
    // DECLARE_SUBSCRIBER(xverack);
};

#undef DEFINE_SUBSCRIBER_TYPE
#undef DEFINE_SUBSCRIBER_OVERLOAD
#undef DECLARE_SUBSCRIBER

} // namespace kth::network

#endif
