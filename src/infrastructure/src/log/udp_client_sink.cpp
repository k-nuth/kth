// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/log/udp_client_sink.hpp>

#include <cstddef>
#include <functional>
#include <string>

#include <boost/make_shared.hpp>

#include <kth/infrastructure/error.hpp>
#include <utility>

namespace kth::log {

using namespace std::placeholders;
using namespace ::asio;
using namespace boost::log;

udp_client_sink::udp_client_sink(socket_ptr socket, endpoint_ptr endpoint)
    : socket_(std::move(socket)), endpoint_(std::move(endpoint))
{}

void udp_client_sink::consume(const record_view& record, std::string const& message) {
    send(message);
}

void udp_client_sink::send(std::string const& message) {
    if ( ! socket_ || !endpoint_) {
        return;
    }

    auto const payload = boost::make_shared<std::string>(message);

    // This is not guarded against interleaving of messages that exceed 64k.
    socket_->async_send_to(buffer(*payload), *endpoint_, std::bind(&udp_client_sink::handle_send, this, _1, _2, payload));
}

void udp_client_sink::handle_send(boost_code const& /*unused*/, size_t /*unused*/, message_ptr payload) {
    // This holds the message in scope until the send is completed.
    payload.reset();
}

} // namespace kth::log
