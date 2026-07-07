// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CONFIG_PARSER_HPP
#define KTH_DOMAIN_CONFIG_PARSER_HPP

#include <algorithm>
#include <cstdint>

#include <kth/domain/define.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/config/checkpoint.hpp>

namespace kth::domain::config {

using kth::operator""_hash;  // For checkpoint hash literals

/// Default hardcoded checkpoints for the given network.
KD_API
kth::infrastructure::config::checkpoint::list default_checkpoints(config::network network);

/// Merge hardcoded checkpoints for `identifier` in front of `checkpoints`,
/// dropping any user-supplied entry at or below the highest hardcoded height.
inline
void fix_checkpoints(uint32_t identifier,
                     kth::infrastructure::config::checkpoint::list& checkpoints,
                     bool is_chipnet) {
    auto const def_checkpoints = default_checkpoints(get_network(identifier, is_chipnet));

    auto const it = std::max_element(def_checkpoints.begin(), def_checkpoints.end(),
        [](auto const& x, auto const& y) { return x.height() < y.height(); });

    if (it == def_checkpoints.end()) {
        return;
    }

    auto const max_height = it->height();
    checkpoints.erase(
        std::remove_if(checkpoints.begin(), checkpoints.end(),
            [max_height](auto const& x) { return x.height() <= max_height; }),
        checkpoints.end());
    checkpoints.insert(checkpoints.begin(),
                       def_checkpoints.begin(), def_checkpoints.end());
}

} // namespace kth::domain::config

#endif
