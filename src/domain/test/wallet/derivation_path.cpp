// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <array>
#include <string>
#include <string_view>
#include <kth/domain/wallet/derivation_path.hpp>
#include <kth/domain/wallet/hd_private.hpp>
#include <kth/domain/wallet/hd_public.hpp>

using namespace kth;
using namespace kth::domain::wallet;

namespace {
constexpr auto parsed_count(std::string_view path) {
    std::array<uint32_t, 255> indexes{};
    return parse_derivation_path(path, indexes);
}

static_assert(parsed_count("m").value() == 0);
static_assert(parsed_count("m/0'/1/2").value() == 3);
static_assert(!parsed_count("m/1abc"));
static_assert(!parsed_count("m/1.5"));
static_assert(!parsed_count("m/-1"));
static_assert(!parsed_count("m/"));
static_assert(parsed_count("m/2147483648'").error() == derivation_path_error::index_out_of_range);
static_assert([] {
    std::array<uint32_t, 3> indexes{};
    auto const count = parse_derivation_path("m/0'/1/2147483647'", indexes);
    return count && *count == 3 && indexes[0] == 0x80000000u && indexes[1] == 1u && indexes[2] == 0xffffffffu;
}());
static_assert([] {
    std::array<uint32_t, 1> indexes{};
    return parse_derivation_path("m/0/1", indexes).error() == derivation_path_error::insufficient_space;
}());
static_assert([] {
    std::array<char, 513> path{};
    path[0] = 'm';
    for (size_t i = 1; i < path.size(); i += 2) { path[i] = '/'; path[i + 1] = '0'; }
    return parsed_count(std::string_view(path.data(), 511)).value() == 255 &&
           parsed_count(std::string_view(path.data(), 513)).error() == derivation_path_error::depth_exceeded;
}());
} // namespace

TEST_CASE("derivation path parses complete decimal components and hardening", "[derivation path]") {
    auto const path = parse_derivation_path("m/0'/1/2147483647'");
    REQUIRE(path);
    REQUIRE(*path == std::vector<uint32_t>{0x80000000u, 1u, 0xffffffffu});
    REQUIRE(parse_derivation_path("m")->empty());
    REQUIRE(*parse_derivation_path("0/42") == std::vector<uint32_t>{0u,42u});
    REQUIRE(*parse_derivation_path("m/0001") == std::vector<uint32_t>{1u});
}

TEST_CASE("derivation path rejects malformed syntax and index overflow", "[derivation path]") {
    for (auto const path : {"", "m/", "/1", "m//1", "m/1/", "m/m/1", "m/1/m", "M/1",
                            "m/1abc", "m/1.5", "m/1e3", "m/-1", "m/+1", "m/ 1", "m/1 ",
                            "m/2147483648", "m/2147483648'", "m/4294967296", "m/'", "m/0''", "m/0h", "m/0H"}) {
        INFO(path);
        REQUIRE_FALSE(parse_derivation_path(path));
    }
    std::string const nul{"m/1\0/2", 6};
    REQUIRE_FALSE(parse_derivation_path(nul));
}

TEST_CASE("derivation path enforces BIP32 depth before deriving keys", "[derivation path]") {
    std::string path = "m";
    for (unsigned i=0;i<255;++i) path += "/0";
    REQUIRE(parse_derivation_path(path)->size() == 255);
    REQUIRE_FALSE(parse_derivation_path(path + "/0"));
    auto const seed = decode_base16("000102030405060708090a0b0c0d0e0f");
    REQUIRE(seed);
    auto const master = hd_private::from_seed(*seed, hd_private::mainnet);
    REQUIRE(master);
    auto const child = master->derive_private(0);
    REQUIRE(child);
    REQUIRE_FALSE(child->derive_path(std::string_view(path).substr(2)));
    REQUIRE_FALSE(child->to_public().derive_path(std::string_view(path).substr(2)));
}

TEST_CASE("derivation path matches BIP32 vector one and leaves its source unchanged", "[derivation path]") {
    auto const seed = decode_base16("000102030405060708090a0b0c0d0e0f");
    REQUIRE(seed);
    auto const master = hd_private::from_seed(*seed, hd_private::mainnet);
    REQUIRE(master);
    auto const before = master->to_string();
    auto const account = master->derive_path("m/0'/1/2'/2/1000000000");
    REQUIRE(account);
    REQUIRE(account->to_string() == "xprvA41z7zogVVwxVSgdKUHDy1SKmdb533PjDz7J6N6mV6uS3ze1ai8FHa8kmHScGpWmj4WggLyQjgPie1rFSruoUihUZREPSL39UNdE3BBDu76");
    REQUIRE(master->to_string() == before);
    REQUIRE(master->derive_path("m")->to_string() == before);
    REQUIRE(hd_private::from_seed_and_path(*seed, "m", hd_private::mainnet)->to_string() == before);
    REQUIRE(master->derive_path("0'")->to_string() == master->derive_private(hd_first_hardened_key)->to_string());
    REQUIRE_FALSE(account->derive_path("m/0"));
    auto const direct = hd_private::from_seed_and_path(*seed, "m/0'/1/2'/2/1000000000", hd_private::mainnet);
    REQUIRE(direct);
    REQUIRE(direct->to_string() == account->to_string());
    REQUIRE_FALSE(hd_private::from_seed_and_path(*seed, "m/1junk", hd_private::mainnet));
}

TEST_CASE("public derivation paths match private derivation and reject hardening", "[derivation path]") {
    auto const seed = decode_base16("000102030405060708090a0b0c0d0e0f");
    REQUIRE(seed);
    auto const master = hd_private::from_seed(*seed, hd_private::mainnet);
    REQUIRE(master);
    auto const public_master = master->to_public();
    auto const child = public_master.derive_path("m/1/2");
    REQUIRE(child);
    REQUIRE(child->to_string() == master->derive_path("m/1/2")->to_public().to_string());
    REQUIRE_FALSE(public_master.derive_path("m/0/1'"));
    REQUIRE_FALSE(child->derive_path("m"));
}

TEST_CASE("derivation paths preserve network prefixes and support relative child paths", "[derivation path]") {
    auto const seed = decode_base16("000102030405060708090a0b0c0d0e0f");
    REQUIRE(seed);
    auto const master = hd_private::from_seed(*seed, hd_private::testnet);
    REQUIRE(master);
    auto const account = hd_private::from_seed_and_path(*seed, "m/1/2", hd_private::testnet);
    REQUIRE(account);
    REQUIRE(account->lineage().prefixes == hd_private::testnet);
    REQUIRE(account->to_public().lineage().prefixes == hd_public::testnet);
    auto const first = master->derive_private(1);
    REQUIRE(first);
    REQUIRE(first->derive_path("2")->to_string() == account->to_string());
    REQUIRE(first->to_public().derive_path("2")->to_string() == account->to_public().to_string());
}
