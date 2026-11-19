// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// kth-wallet: wallet support answered from peers, with the local Electrum
// listener for wallets that do not speak P2P. It keeps headers and filters
// and downloads whole blocks where they match; it names nothing it watches to
// any peer. Prints "electrum <port>" once listening.
//
//   kth-wallet --network regtest --directory DIR --peer HOST:PORT [--peer ...]
//              [--electrum LOOPBACK:PORT] [--birthday HEIGHT] [--sent-log FILE]
//              [--no-isolate] [--decoys N] [--download-all] [--scan-threads N]
//              [--input-prefix-filters]

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_future.hpp>

#include <kth/node/wallet/electrum.hpp>
#include <kth/node/wallet/service.hpp>

namespace {

using kth::domain::config::network;

std::optional<network> network_of(std::string_view name) {
    if (name == "mainnet") return network::mainnet;
    if (name == "testnet") return network::testnet;
    if (name == "regtest") return network::regtest;
#if defined(KTH_CURRENCY_BCH)
    if (name == "testnet4") return network::testnet4;
    if (name == "scalenet") return network::scalenet;
    if (name == "chipnet") return network::chipnet;
#endif
    return std::nullopt;
}

int usage() {
    std::fprintf(stderr, "usage: kth-wallet --network NAME --directory DIR --peer HOST:PORT [--peer ...] "
        "[--electrum LOOPBACK:PORT] [--birthday HEIGHT] [--sent-log FILE] [--no-isolate] [--decoys N] "
        "[--download-all] [--scan-threads N] [--input-prefix-filters]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    namespace wallet = kth::node::wallet;
    wallet::peer_settings settings;
    std::string electrum = "127.0.0.1:0";
    std::vector<std::string_view> args(argv + 1, argv + argc);
    for (size_t at = 0; at < args.size(); ++at) {
        auto const flag = args[at];
        auto const value = [&]() -> std::string {
            if (at + 1 >= args.size()) return {};
            return std::string(args[++at]);
        };
        if (flag == "--network") {
            auto const net = network_of(value());
            if ( ! net) return usage();
            settings.network = *net;
        } else if (flag == "--directory") {
            settings.directory = value();
        } else if (flag == "--peer") {
            settings.peers.push_back(value());
        } else if (flag == "--electrum") {
            electrum = value();
        } else if (flag == "--birthday") {
            settings.birthday = uint32_t(std::strtoul(value().c_str(), nullptr, 10));
        } else if (flag == "--sent-log") {
            settings.sent_log = value();
        } else if (flag == "--no-isolate") {
            settings.isolate_downloads = false;
        } else if (flag == "--decoys") {
            settings.decoy_blocks = size_t(std::strtoul(value().c_str(), nullptr, 10));
        } else if (flag == "--download-all") {
            settings.download_all_blocks = true;
        } else if (flag == "--scan-threads") {
            settings.scan_threads = size_t(std::strtoul(value().c_str(), nullptr, 10));
        } else if (flag == "--input-prefix-filters") {
            settings.input_prefix_filters = true;
        } else {
            return usage();
        }
    }
    if (settings.directory.empty() || settings.peers.empty()) return usage();
    // A loopback address only ([::1] without its brackets): the listener is
    // for this machine's wallets.
    auto const bind = wallet::parse_electrum_bind(electrum);
    if ( ! bind) {
        std::fprintf(stderr, "kth-wallet: --electrum is refused: %s\n", bind.error().c_str());
        return 2;
    }
    auto const& [address, port] = *bind;

    auto made = wallet::service::from_peers(settings);
    if ( ! made) {
        std::fprintf(stderr, "kth-wallet: cannot start: %s\n", made.error().message().c_str());
        return 1;
    }
    auto& service = **made;
    auto running = ::asio::co_spawn(service.executor(), service.run(), ::asio::use_future);
    {
        wallet::electrum_server listener(service, settings.network);
        auto const bound = listener.listen(address, port);
        if ( ! bound) {
            std::fprintf(stderr, "kth-wallet: cannot listen on %s: %s\n", electrum.c_str(), bound.error().message().c_str());
            service.stop();
            (void)running.wait_for(std::chrono::seconds(30));
            return 1;
        }
        ::asio::co_spawn(service.executor(), listener.run(), ::asio::detached);
        std::printf("electrum %u\n", unsigned(*bound));
        std::fflush(stdout);

        // Where it stands, every two seconds, as one line of JSON: heights,
        // peers, and the bytes it sent and received.
        ::asio::io_context signals_context;
        ::asio::signal_set signals(signals_context, SIGINT, SIGTERM);
        ::asio::steady_timer report(signals_context);
        std::function<void()> print_status = [&] {
            auto const now = service.current();
            auto const height = [](std::optional<uint32_t> const& value) {
                return value ? std::to_string(*value) : std::string("null");
            };
            std::printf("status {\"header_height\":%u,\"tip\":%s,\"input_prefix_tip\":%s,\"peers\":%zu,"
                "\"synced\":%s,\"bytes_received\":%llu,\"bytes_sent\":%llu,\"blocks_downloaded\":%llu,"
                "\"filter_bytes\":%llu}\n", now.header_height, height(now.tip).c_str(), height(now.input_prefix_tip).c_str(),
                now.peers, now.synced ? "true" : "false", (unsigned long long)now.bytes_received,
                (unsigned long long)now.bytes_sent, (unsigned long long)now.blocks_downloaded,
                (unsigned long long)now.filter_bytes);
            std::fflush(stdout);
            report.expires_after(std::chrono::seconds(2));
            report.async_wait([&](std::error_code ec) {
                if ( ! ec) print_status();
            });
        };
        print_status();
        signals.async_wait([&](std::error_code, int) { report.cancel(); });
        signals_context.run();
        listener.stop();
        service.stop();
        (void)running.wait_for(std::chrono::seconds(30));
    }
    return 0;
}
