// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/parser.hpp>
#include <asio/io_context.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/experimental/channel_error.hpp>
#include <cstdio>
#include <stdexcept>

int main() {
    for (auto net : {kth::domain::config::network::regtest,
                     kth::domain::config::network::mainnet}) {
        kth::node::parser parser(net);
        if (parser.configured.network.inbound_connections != 0 ||
            parser.configured.network.outbound_connections != 0 ||
            !parser.configured.network.seeds.empty()) {
            std::fputs("Browser parser enabled unsupported automatic connections/seeds\n", stderr);
            return 1;
        }
    }

    // The pinned Asio channel must wake a receive in either shutdown ordering.
    // poll() bounds the control without blocking if the wakeup is lost.
    for (bool close_first : {false, true}) {
        asio::io_context context;
        asio::experimental::concurrent_channel<void(std::error_code, int)> channel(context, 1);
        if (close_first) channel.close();
        bool completed = false;
        channel.async_receive([&](std::error_code ec, int) {
            if (ec != asio::experimental::error::channel_closed) {
                throw std::runtime_error("Unexpected channel shutdown result");
            }
            completed = true;
        });
        if (!close_first) {
            context.poll();
            if (completed) throw std::runtime_error("Receive completed before shutdown");
            channel.close();
        }
        context.restart();
        context.poll();
        if (!completed) throw std::runtime_error("Channel close left a receive pending");
    }
    std::puts("PASS browser parser defaults and subscription close orderings");
}
