// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/checkpoint.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

#include <ctre.hpp>
#include <fmt/core.h>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::infrastructure::config {

// static
std::expected<checkpoint, kth::code> checkpoint::parse_from(std::string_view value) {
    constexpr auto pattern = ctll::fixed_string{R"(^([0-9a-f]{64})(:([0-9]{1,20}))?$)"};

    auto match = ctre::match<pattern>(value);
    if ( ! match) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto const hash_sv = match.get<1>().to_view();
    auto hash = decode_hash(hash_sv);
    if ( ! hash) {
        return std::unexpected(kth::error::illegal_value);
    }

    size_t height = 0;
    auto const height_group = match.get<3>();
    if (height_group) {
        auto const sv = height_group.to_view();
        auto const result = std::from_chars(sv.data(), sv.data() + sv.size(), height);
        if (result.ec != std::errc()) {
            return std::unexpected(kth::error::illegal_value);
        }
    }

    return checkpoint{*hash, height};
}

// static
std::expected<checkpoint, kth::code> checkpoint::parse_from(std::string_view hash,
                                                            size_t height) {
    auto decoded = decode_hash(hash);
    if ( ! decoded) {
        return std::unexpected(kth::error::illegal_value);
    }
    return checkpoint{*decoded, height};
}

std::string checkpoint::to_string() const {
    return fmt::format("{}:{}", encode_hash(hash_), height_);
}

// static
bool checkpoint::covered(size_t height, list const& checks) {
    return ! checks.empty() && height <= checks.back().height();
}

// static
bool checkpoint::validate(hash_digest const& hash, size_t height, list const& checks) {
    auto const contradicts = [&](checkpoint const& item) {
        return height == item.height() && hash != item.hash();
    };
    return std::find_if(checks.begin(), checks.end(), contradicts) == checks.end();
}

} // namespace kth::infrastructure::config
