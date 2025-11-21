// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/proxy.hpp>

#define BOOST_BIND_NO_PLACEHOLDERS

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <utility>
#include <kth/domain.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/network/define.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define NAME "proxy"

using namespace std::placeholders;
using namespace ::asio;
using namespace kd::message;

// Dump up to 1k of payload as hex in order to diagnose failure.
static size_t const invalid_payload_dump_size = 1024;

// payload_buffer_ sizing assumes monotonically increasing size by version.
// Initialize to pre-witness max payload and let grow to witness as required.
// The socket owns the single thread on which this channel reads and writes.
proxy::proxy(threadpool& pool, socket::ptr socket, settings const& settings)
    : authority_(socket->authority())
    , heading_buffer_(heading::maximum_size())
    , payload_buffer_(heading::maximum_payload_size(settings.protocol_maximum, settings.identifier, settings.inbound_port == 48333))
    , maximum_payload_(heading::maximum_payload_size(settings.protocol_maximum, settings.identifier, settings.inbound_port == 48333))
    , socket_(socket)
    , stopped_(true)
    , protocol_magic_(settings.identifier)
    , validate_checksum_(settings.validate_checksum)
    , verbose_(settings.verbose)
    , version_(settings.protocol_maximum)
    , message_subscriber_(pool)
    , stop_subscriber_(std::make_shared<stop_subscriber>(pool, NAME "_sub"))
    , dispatch_(pool, NAME "_dispatch")
{}

proxy::~proxy() {
    KTH_ASSERT_MSG(stopped(), "The channel was not stopped.");
}

// Properties.
// ----------------------------------------------------------------------------

const infrastructure::config::authority& proxy::authority() const {
    return authority_;
}

uint32_t proxy::negotiated_version() const {
    return version_.load();
}

void proxy::set_negotiated_version(uint32_t value) {
    version_.store(value);
}

// Start sequence.
// ----------------------------------------------------------------------------

void proxy::start(result_handler handler) {
    if ( ! stopped()) {
        handler(error::operation_failed);
        return;
    }

    stopped_ = false;
    stop_subscriber_->start();
    message_subscriber_.start();

    // Allow for subscription before first read, so no messages are missed.
    handler(error::success);

    // Start the read cycle.
    read_heading();
}

// Stop subscription.
// ----------------------------------------------------------------------------

void proxy::subscribe_stop(result_handler handler) {
    stop_subscriber_->subscribe(handler, error::channel_stopped);
}

// Read cycle (read continues until stop).
// ----------------------------------------------------------------------------

void proxy::read_heading() {
    if (stopped())
        return;

    async_read(socket_->get(), buffer(heading_buffer_),
        std::bind(&proxy::handle_read_heading,
            shared_from_this(), _1, _2));
}

void proxy::handle_read_heading(boost_code const& ec, size_t) {
    if (stopped()) {
        return;
    }

    if (ec) {
        spdlog::debug("[network] Heading read failure [{}] {}", authority(), code(error::boost_to_error_code(ec)).message());
        stop(ec);
        return;
    }

    // Using byte_reader instead of deprecated create_old function
    byte_reader reader(heading_buffer_);
    auto head_res = heading::from_data(reader, 0);
    if ( ! head_res) {
        spdlog::warn("[network] Failed to parse heading from [{}]", authority());
        stop(error::bad_stream);
        return;
    }
    auto const head = *head_res;

    if ( ! head.is_valid()) {
        spdlog::warn("[network] Invalid heading from [{}]", authority());
        stop(error::bad_stream);
        return;
    }

    if (head.magic() != protocol_magic_) {
        // These are common, with magic 542393671 coming from http requests.
        spdlog::debug("[network] Invalid heading magic ({}) from [{}]", head.magic(), authority());
        stop(error::bad_stream);
        return;
    }

    if (head.payload_size() > max_payload_size) {
        spdlog::debug("[network] Huge payload indicated by {} heading from [{}] ({} bytes)", head.command(), authority(), head.payload_size());
    }

    if (head.payload_size() > maximum_payload_) {
        spdlog::debug("[network] Oversized payload indicated by {} heading from [{}] ({} bytes)", head.command(), authority(), head.payload_size());
        stop(error::bad_stream);
        return;
    }

    read_payload(head);
}

void proxy::read_payload(heading const& head) {
    if (stopped()) {
        return;
    }

    // This does not cause a reallocation.
    payload_buffer_.resize(head.payload_size());

    async_read(socket_->get(), buffer(payload_buffer_), std::bind(&proxy::handle_read_payload, shared_from_this(), _1, _2, head));
}

void proxy::handle_read_payload(boost_code const& ec, size_t payload_size, heading const& head) {
    if (stopped()) return;

    if (ec) {
        spdlog::debug("[network] Payload read failure [{}] {}", authority(), code(error::boost_to_error_code(ec)).message());
        stop(ec);
        return;
    }

    // This is a pointless test but we allow it as an option for completeness.
    if (validate_checksum_ && head.checksum() != bitcoin_checksum(payload_buffer_)) {
        spdlog::warn("[network] Invalid {} payload from [{}] bad checksum.", head.command(), authority());
        stop(error::bad_stream);
        return;
    }

    spdlog::debug("[network] Read {} from [{}] ({} bytes). Now parsing ...", head.command(), authority(), payload_size);

    // Notify subscribers of the new message.
    byte_reader reader(payload_buffer_);

    // Failures are not forwarded to subscribers and channel is stopped below.
    auto const code = message_subscriber_.load(head.type(), version_, reader);
    auto const consumed = reader.is_exhausted();

    if (verbose_ && code) {
        auto const size = std::min(payload_size, invalid_payload_dump_size);
        auto const begin = payload_buffer_.begin();

        spdlog::trace("[network] Invalid payload from [{}] {}", authority(), encode_base16(data_chunk{ begin, begin + size }));
        stop(code);
        return;
    }

    if (code) {
        spdlog::trace("[network] Invalid {} payload from [{}] {}", head.command(), authority(), code.message());
        stop(code);
        return;
    }

    if ( ! consumed) {
        spdlog::trace("[network] Invalid {} payload from [{}] trailing bytes.", head.command(), authority());
        stop(error::bad_stream);
        return;
    }

    spdlog::debug("[network] Received {} from [{}] ({} bytes)", head.command(), authority(), payload_size);

    signal_activity();
    read_heading();
}

// Message send sequence.
// ----------------------------------------------------------------------------

void proxy::do_send(command_ptr command, payload_ptr payload, result_handler handler) {
    async_write(socket_->get(), buffer(*payload),
        std::bind(&proxy::handle_send,
            shared_from_this(), _1, _2, command, payload, handler));
}

void proxy::handle_send(boost_code const& ec, size_t, command_ptr command, payload_ptr payload, result_handler handler) {
    dispatch_.unlock();
    auto const size = payload->size();
    auto const error = code(error::boost_to_error_code(ec));

    if (stopped()) {
        handler(error);
        return;
    }

    if (error) {
        spdlog::debug("[network] Failure sending {} to [{}] ({} bytes) {}", *command, authority(), size, error.message());
        stop(error);
        handler(error);
        return;
    }

    spdlog::trace("[network] Sent {} to [{}] ({} bytes)", *command, authority(), size);

    handler(error);
}

// Stop sequence.
// ----------------------------------------------------------------------------

// This is not short-circuited by a stop test because we need to ensure it
// completes at least once before invoking the handler. That would require a
// lock be taken around the entire section, which poses a deadlock risk.
// Instead this is thread safe and idempotent, allowing it to be unguarded.
void proxy::stop(code const& ec) {
    KTH_ASSERT_MSG(ec, "The stop code must be an error code.");

    stopped_ = true;

    // Prevent subscription after stop.
    message_subscriber_.stop();
    message_subscriber_.broadcast(error::channel_stopped);

    // Prevent subscription after stop.
    stop_subscriber_->stop();
    stop_subscriber_->relay(ec);

    // Give channel opportunity to terminate timers.
    handle_stopping();

    // Signal socket to stop reading and accepting new work.
    socket_->stop();
}

bool proxy::stopped() const {
    return stopped_;
}

} // namespace kth::network