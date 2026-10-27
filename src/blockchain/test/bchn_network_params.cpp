// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Per-network consensus parameters of Bitcoin Cash Node (src/chainparams.cpp, commit
// 07576013c91ff4a3a74acd85f189c69121cdad1b) that Knuth keeps in blockchain::settings:
// the ASERT half life, the default consensus block size and the ABLA configuration
// (fixed size on testnet3 and testnet4). BCHN asserts the same invariants when it
// builds each CChainParams: the ABLA state at activation gives the default size.
// Original: Copyright (c) 2009-present The Bitcoin Core / Bitcoin developers, MIT license.

#include <test_helpers.hpp>

#include <kth/blockchain/settings.hpp>
#include <kth/domain/chain/abla.hpp>

#if defined(KTH_CURRENCY_BCH)

using namespace kth;
namespace abla = kth::domain::chain::abla;
using kth::domain::config::network;

TEST_CASE("BCHN chainparams - ASERT half life, default block size and ABLA configuration per network",
          "[bchn_non_vm][network_params][abla]") {
    struct expected {
        char const* name;
        network net;
        uint64_t half_life;
        uint64_t default_size;
        bool fixed_abla;
    };
    constexpr uint64_t one_megabyte = 1'000'000;
    constexpr uint64_t two_days = 2ull * 24 * 60 * 60;
    constexpr uint64_t one_hour = 60ull * 60;
    static expected const rows[] = {
        {"main", network::mainnet, two_days, 32 * one_megabyte, false},
        {"test (testnet3)", network::testnet, one_hour, 32 * one_megabyte, true},
        {"test4", network::testnet4, one_hour, 2 * one_megabyte, true},
        {"scale", network::scalenet, two_days, 256 * one_megabyte, false},
        {"chip", network::chipnet, one_hour, 2 * one_megabyte, false},
        {"regtest", network::regtest, two_days, 32 * one_megabyte, false},
    };
    for (auto const& r : rows) {
        DYNAMIC_SECTION(r.name) {
            blockchain::settings const s(r.net);
            CHECK(s.asert_half_life == r.half_life);
            CHECK(s.default_consensus_block_size == r.default_size);
            CHECK(abla::validate(s.abla_config) == abla::config_validity::valid);
            // assert(abla::State(consensus.ablaConfig, 0).GetBlockSizeLimit() == nDefaultConsensusBlockSize)
            CHECK(abla::block_size_limit(abla::state(s.abla_config, 0)) == r.default_size);
            // assert(consensus.ablaConfig.IsFixedSize()) / assert(!...IsFixedSize())
            bool const fixed = s.abla_config.epsilon0 == s.abla_config.epsilon_max && s.abla_config.beta0 == s.abla_config.beta_max;
            CHECK(fixed == r.fixed_abla);
            // The configuration is the one MakeDefault builds from the default size
            auto const reference = abla::default_config(r.default_size, r.fixed_abla);
            CHECK(s.abla_config.epsilon0 == reference.epsilon0);
            CHECK(s.abla_config.beta0 == reference.beta0);
            CHECK(s.abla_config.epsilon_max == reference.epsilon_max);
            CHECK(s.abla_config.beta_max == reference.beta_max);
            CHECK(s.abla_config.zeta_xB7 == reference.zeta_xB7);
            CHECK(s.abla_config.gamma_reciprocal == reference.gamma_reciprocal);
            CHECK(s.abla_config.theta_reciprocal == reference.theta_reciprocal);
            CHECK(s.abla_config.delta == reference.delta);
        }
    }
}

#endif // defined(KTH_CURRENCY_BCH)
