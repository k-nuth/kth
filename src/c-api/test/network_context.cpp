// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#include <memory>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include <kth/capi/config/settings.h>
#include <kth/capi/config/settings_helper.hpp>
#include <kth/capi/conversions/config.hpp>

TEST_CASE("C settings preserve selected network consensus defaults", "[capi][network_context]") {
    for (auto net : {kth_network_mainnet, kth_network_testnet, kth_network_regtest
#if defined(KTH_CURRENCY_BCH)
        , kth_network_testnet4, kth_network_scalenet, kth_network_chipnet
#endif
    }) {
        CAPTURE(static_cast<int>(net));
        auto raw = new kth_settings(kth_config_settings_default(net));
        std::unique_ptr<kth_settings, decltype(&kth_config_settings_destruct)> settings(raw, kth_config_settings_destruct);
        auto const actual = kth::capi::helpers::settings_to_cpp(*settings);
        kth::node::configuration const expected(kth::network_to_cpp(net));
        CHECK(actual.net == expected.net);
#if defined(KTH_CURRENCY_BCH)
        CHECK(actual.chain.default_consensus_block_size == expected.chain.default_consensus_block_size);
        CHECK(actual.chain.abla_config.epsilon0 == expected.chain.abla_config.epsilon0);
        CHECK(actual.chain.abla_config.beta0 == expected.chain.abla_config.beta0);
        CHECK(actual.chain.abla_config.epsilon_max == expected.chain.abla_config.epsilon_max);
        CHECK(actual.chain.abla_config.beta_max == expected.chain.abla_config.beta_max);
#endif
    }
}

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("C settings loaded from a file rebuild selected network defaults before overrides", "[capi][network_context][file_network]") {
    struct owned_directory {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("kth_capi_file_network_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        bool owned = std::filesystem::create_directory(directory);
        ~owned_directory() { if (owned) { std::error_code ec; std::filesystem::remove_all(directory, ec); } }
    } storage;
    REQUIRE(storage.owned);
    for (auto const network : {"chipnet"}) {
        for (bool const overrides : {false, true}) {
            CAPTURE(network, overrides);
            auto const file = storage.directory / (std::string(network) + (overrides ? "-overrides.cfg" : ".cfg"));
            {
                std::ofstream output(file);
                output << "network=" << network << '\n';
                if (overrides) {
                    output << "net.inbound_port=12345\nchain.cores=3\nfork.asert_half_life=1234\ndb.directory=selected-database\n";
                }
                REQUIRE(output.good());
            }
            kth_settings* raw = nullptr;
            char* raw_error = nullptr;
            auto const ok = kth_config_settings_get_from_file(file.string().c_str(), &raw, &raw_error);
            std::unique_ptr<char, decltype(&free)> error(raw_error, free);
            INFO((error ? error.get() : ""));
            std::unique_ptr<kth_settings, decltype(&kth_config_settings_destruct)> settings(raw, kth_config_settings_destruct);
            REQUIRE(ok);
            REQUIRE(settings);
            auto const selected = kth::domain::config::network::chipnet;
            kth::node::configuration const expected(selected);
            auto const actual = kth::capi::helpers::settings_to_cpp(*settings);
            CHECK(actual.net == selected);
            CHECK(actual.network.identifier == expected.network.identifier);
            CHECK(actual.network.hosts_file == expected.network.hosts_file);
            CHECK(actual.chain.default_consensus_block_size == expected.chain.default_consensus_block_size);
            CHECK(actual.chain.abla_config.epsilon0 == expected.chain.abla_config.epsilon0);
            CHECK(actual.chain.abla_config.beta0 == expected.chain.abla_config.beta0);
            CHECK(actual.chain.cantor_activation_time == expected.chain.cantor_activation_time);
            CHECK(actual.chain.checkpoints == expected.chain.checkpoints);
            CHECK(actual.network.inbound_port == (overrides ? 12345 : expected.network.inbound_port));
            CHECK(actual.chain.cores == (overrides ? 3 : expected.chain.cores));
            CHECK(actual.chain.asert_half_life == (overrides ? 1234 : expected.chain.asert_half_life));
            CHECK(actual.database.directory == (overrides ? kth::path{"selected-database"} : expected.database.directory));
        }
    }
}
#endif

#if defined(KTH_CURRENCY_BCH) && __has_include(<kth/domain/chain/block_ticks.hpp>)
TEST_CASE("C settings preserve tempnet identity and unexposed network defaults", "[capi][network_context]") {
    auto raw = new kth_settings(kth_config_settings_default(kth_network_tempnet));
    std::unique_ptr<kth_settings, decltype(&kth_config_settings_destruct)> settings(raw, kth_config_settings_destruct);
    settings->network.inbound_port = 12345;
    settings->chain.cores = 3;
    auto const actual = kth::capi::helpers::settings_to_cpp(*settings);
    kth::node::configuration const expected(kth::domain::config::network::tempnet);
    CHECK(actual.net == expected.net);
    CHECK(actual.network.context == expected.net);
    CHECK(actual.network.inbound_port == 12345);
    CHECK(actual.chain.cores == 3);
    CHECK(actual.network.peers_file == expected.network.peers_file);
    CHECK(actual.network.banlist_file == expected.network.banlist_file);
    CHECK(actual.chain.default_consensus_block_size == expected.chain.default_consensus_block_size);
    CHECK(actual.chain.abla_config.epsilon0 == expected.chain.abla_config.epsilon0);
    CHECK(actual.chain.abla_config.beta0 == expected.chain.abla_config.beta0);
    CHECK(actual.chain.cantor_activation_time == expected.chain.cantor_activation_time);
    CHECK(actual.chain.checkpoints == expected.chain.checkpoints);
}
#endif

#if defined(KTH_CURRENCY_BCH)
TEST_CASE("C settings loaded from a tempnet file rebuild network defaults before overrides", "[capi][network_context][file_network]") {
    struct owned_directory {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("kth_capi_file_network_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        bool owned = std::filesystem::create_directory(directory);
        ~owned_directory() { if (owned) { std::error_code ec; std::filesystem::remove_all(directory, ec); } }
    } storage;
    REQUIRE(storage.owned);
    for (auto const network : {"tempnet"}) {
        for (bool const overrides : {false, true}) {
            CAPTURE(network, overrides);
            auto const file = storage.directory / (std::string(network) + (overrides ? "-overrides.cfg" : ".cfg"));
            {
                std::ofstream output(file);
                output << "network=" << network << '\n';
                if (overrides) {
                    output << "net.inbound_port=12345\nchain.cores=3\nfork.asert_half_life=1234\ndb.directory=selected-database\n";
                }
                REQUIRE(output.good());
            }
            kth_settings* raw = nullptr;
            char* raw_error = nullptr;
            auto const ok = kth_config_settings_get_from_file(file.string().c_str(), &raw, &raw_error);
            std::unique_ptr<char, decltype(&free)> error(raw_error, free);
            INFO((error ? error.get() : ""));
            std::unique_ptr<kth_settings, decltype(&kth_config_settings_destruct)> settings(raw, kth_config_settings_destruct);
            REQUIRE(ok);
            REQUIRE(settings);
            auto const selected = kth::domain::config::network::tempnet;
            kth::node::configuration const expected(selected);
            auto const actual = kth::capi::helpers::settings_to_cpp(*settings);
            CHECK(actual.net == selected);
            CHECK(actual.network.context == selected);
            CHECK(actual.network.identifier == expected.network.identifier);
            CHECK(actual.network.hosts_file == expected.network.hosts_file);
            CHECK(actual.chain.default_consensus_block_size == expected.chain.default_consensus_block_size);
            CHECK(actual.chain.abla_config.epsilon0 == expected.chain.abla_config.epsilon0);
            CHECK(actual.chain.abla_config.beta0 == expected.chain.abla_config.beta0);
            CHECK(actual.chain.cantor_activation_time == expected.chain.cantor_activation_time);
            CHECK(actual.chain.checkpoints == expected.chain.checkpoints);
            CHECK(actual.network.inbound_port == (overrides ? 12345 : expected.network.inbound_port));
            CHECK(actual.chain.cores == (overrides ? 3 : expected.chain.cores));
            CHECK(actual.chain.asert_half_life == (overrides ? 1234 : expected.chain.asert_half_life));
            CHECK(actual.database.directory == (overrides ? kth::path{"selected-database"} : expected.database.directory));
        }
    }
}
#endif
