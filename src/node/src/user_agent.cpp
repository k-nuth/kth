// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/user_agent.hpp>

#include <format>

#include <kth/domain/version.hpp>

namespace kth::node {

namespace {

constexpr uint64_t one_megabyte = 1'000'000;

} // anonymous namespace

std::string format_eb(uint64_t block_size_bytes) {
    // Convert bytes to MB with 1 decimal place (floored)
    // Examples:
    //   32000000 -> "EB32.0"
    //   1540000  -> "EB1.5"
    //   210000   -> "EB0.2"
    auto const units = block_size_bytes / (one_megabyte / 10);
    auto const integer_part = units / 10;
    auto const decimal_part = units % 10;
    return std::format("EB{}.{}", integer_part, decimal_part);
}

std::string get_user_agent(std::vector<std::string> const& features) {
    // Format: /ClientName:Version(feature1; feature2; ...)/
    // Example: /Knuth:0.75.0(EB32.0)/
    // BCHN style: /Bitcoin Cash Node:28.0.1(EB32.0)/
    if (features.empty()) {
        return std::format("/{}:{}/", kth::client_name, kth::version);
    }

    std::string features_str;
    for (size_t i = 0; i < features.size(); ++i) {
        if (i > 0) {
            features_str += "; ";
        }
        features_str += features[i];
    }

    return std::format("/{}:{}({})/", kth::client_name, kth::version, features_str);
}

} // namespace kth::node
