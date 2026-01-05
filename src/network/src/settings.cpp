// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/settings.hpp>

#include <kth/domain.hpp>
#include <kth/domain/multi_crypto_support.hpp>

namespace kth::network {

using namespace kth::asio;
using namespace kd::message;

// Common default values (no settings context).
settings::settings()
    : threads(0)
    , protocol_maximum(version::level::maximum)
    , protocol_minimum(version::level::minimum)
    , services(version::service::node_network)
#if defined(KTH_CURRENCY_BCH)
    , invalid_services(0)
#else
    , invalid_services(176)
#endif
    , relay_transactions(true)
    , validate_checksum(false)
    , inbound_connections(0)
    , outbound_connections(8)
    , manual_attempt_limit(0)
    , connect_batch_size(5)
    , connect_timeout_seconds(5)
    , channel_handshake_seconds(6000)
    , channel_heartbeat_minutes(5)
    , channel_inactivity_minutes(10)
    , channel_expiration_minutes(60)
    , channel_germination_seconds(30)
    , host_pool_capacity(1000)
    , hosts_file("hosts.cache")
    , self(unspecified_network_address)
    // , bitcoin_cash(false)

    // [log]
    , debug_file("debug.log")
    , error_file("error.log")
    , archive_directory("archive")
    , rotation_size(0)
    , minimum_free_space(0)
    , maximum_archive_size(0)
    , maximum_archive_files(0)
    , statistics_server(unspecified_network_address)
    , verbose(false)
    , use_ipv6(true)
{}

// Use push_back due to initializer_list bug:
// stackoverflow.com/a/20168627/1172329
settings::settings(domain::config::network context)
    : settings() {
    // Handle deviations from common defaults.
    switch (context) {
        case domain::config::network::mainnet: {
// #ifdef LITECOIN
#ifdef KTH_CURRENCY_LTC
            inbound_port = 9333;
            // identifier = 0xdbb6c0fb;
            identifier = netmagic::ltc_mainnet;
            seeds.reserve(5);
            seeds.emplace_back("seed-a.litecoin.loshan.co.uk", 9333);
            seeds.emplace_back("dnsseed.thrasher.io", 9333);
            seeds.emplace_back("dnsseed.litecointools.com", 9333);
            seeds.emplace_back("dnsseed.litecoinpool.org", 9333);
            seeds.emplace_back("dnsseed.koin-project.com", 9333);
#else
            // identifier = 3652501241;
            inbound_port = 8333;

#if defined(KTH_CURRENCY_BCH)
            identifier = netmagic::bch_mainnet;
            seeds.reserve(7);
            seeds.emplace_back("seed.flowee.cash", 8333);                     // Flowee
            seeds.emplace_back("btccash-seeder.bitcoinunlimited.info", 8333); // Bitcoin Unlimited
            seeds.emplace_back("seed.bchd.cash", 8333);                       // BCHD
            seeds.emplace_back("seed.bch.loping.net", 8333);                  // Loping.net
            seeds.emplace_back("dnsseed.electroncash.de", 8333);              // Electroncash.de
            seeds.emplace_back("bchseed.c3-soft.com", 8333);                  // C3 Soft (NilacTheGrim)
            seeds.emplace_back("bch.bitjson.com", 8333);                      // Jason Dreyzehner
#else
            identifier = netmagic::btc_mainnet;
            seeds.reserve(6);
            seeds.emplace_back("seed.bitcoin.sipa.be", 8333);
            seeds.emplace_back("dnsseed.bluematt.me", 8333);
            seeds.emplace_back("dnsseed.bitcoin.dashjr.org", 8333);
            seeds.emplace_back("seed.bitcoinstats.com", 8333);
            seeds.emplace_back("seed.bitcoin.jonasschnelli.ch", 8333);
            seeds.emplace_back("seed.voskuil.org", 8333);
#endif // KTH_CURRENCY_BCH
#endif //KTH_CURRENCY_LTC
            break;
        }

        case domain::config::network::testnet: {
#ifdef KTH_CURRENCY_LTC
            // identifier = 4056470269;
            identifier = netmagic::ltc_testnet;
            inbound_port = 19335;
            seeds.reserve(2);
            seeds.emplace_back("testnet-seed.litecointools.com", 19335);
            seeds.emplace_back("seed-b.litecoin.loshan.co.uk", 19335);
#else
            // identifier = 118034699;
            inbound_port = 18333;
#if defined(KTH_CURRENCY_BCH)
            identifier = netmagic::bch_testnet;
            seeds.reserve(3);
            seeds.emplace_back("testnet-seed.bchd.cash", 18333);
            seeds.emplace_back("seed.tbch.loping.net", 18333);
            seeds.emplace_back("testnet-seed.bitcoinunlimited.info", 18333);
#else
            identifier = netmagic::btc_testnet;
            // Seeds based on satoshi client v0.14.0 plus voskuil.org.
            seeds.reserve(5);
            seeds.emplace_back("testnet-seed.bitcoin.jonasschnelli.ch", 18333);
            seeds.emplace_back("seed.tbtc.petertodd.org", 18333);
            seeds.emplace_back("testnet-seed.bluematt.me", 18333);
            seeds.emplace_back("testnet-seed.bitcoin.schildbach.de", 18333);
            seeds.emplace_back("testnet-seed.voskuil.org", 18333);
#endif //KTH_CURRENCY_BCH
#endif //KTH_CURRENCY_LTC
            break;
        }

        case domain::config::network::regtest: {
            identifier = 3669344250;        //TODO(fernando): use appropiate constant
            inbound_port = 18444;

            // Regtest is private network only, so there is no seeding.
            break;
        }

#if defined(KTH_CURRENCY_BCH)
        case domain::config::network::testnet4: {
            inbound_port = 28333;
            identifier = netmagic::bch_testnet4;
            seeds.reserve(4);
            seeds.emplace_back("testnet4-seed-bch.toom.im", 28333);
            seeds.emplace_back("seed.tbch4.loping.net", 28333);
            seeds.emplace_back("testnet4-seed.flowee.cash", 28333);
            seeds.emplace_back("testnet4.bitjson.com", 28333);
            break;
        }

        case domain::config::network::scalenet: {
            inbound_port = 38333;
            identifier = netmagic::bch_scalenet;
            seeds.reserve(2);
            seeds.emplace_back("scalenet-seed-bch.toom.im", 38333);
            seeds.emplace_back("seed.sbch.loping.net", 38333);
            break;
        }

        case domain::config::network::chipnet: {
            inbound_port = 48333;
            identifier = netmagic::bch_chipnet;
            seeds.reserve(3);
            seeds.emplace_back("chipnet.bitjson.com", 48333);
            seeds.emplace_back("chipnet.imaginary.cash", 48333);
            seeds.emplace_back("chipnet.bch.ninja", 48333);
            break;
        }
#endif //KTH_CURRENCY_BCH
    }
}

duration settings::connect_timeout() const {
    return seconds(connect_timeout_seconds);
}

duration settings::channel_handshake() const {
    return seconds(channel_handshake_seconds);
}

duration settings::channel_heartbeat() const {
    return minutes(channel_heartbeat_minutes);
}

duration settings::channel_inactivity() const {
    return minutes(channel_inactivity_minutes);
}

duration settings::channel_expiration() const {
    return minutes(channel_expiration_minutes);
}

duration settings::channel_germination() const {
    return seconds(channel_germination_seconds);
}

} // namespace kth::network
