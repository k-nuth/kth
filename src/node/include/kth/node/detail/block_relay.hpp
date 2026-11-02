// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_BLOCK_RELAY_HPP
#define KTH_NODE_DETAIL_BLOCK_RELAY_HPP

#include <algorithm>
#include <cstdint>
#include <optional>

namespace kth::node::detail {

struct block_relay_range {
    uint32_t first;
    uint32_t last;
};

class block_relay_state {
public:
    explicit block_relay_state(bool caught_up = false) : ready_(caught_up) {}

    [[nodiscard]]
    std::optional<block_relay_range> connected(uint32_t first, uint32_t last, bool synchronized) {
        if ( ! ready_) {
            // The batch that finishes initial sync arms relay, but is still IBD.
            ready_ = synchronized;
            return std::nullopt;
        }

        first_ = first_ ? std::min(*first_, first) : first;
        if ( ! synchronized) {
            return std::nullopt;
        }

        auto const range = block_relay_range{*first_, last};
        first_.reset();
        return range;
    }

private:
    bool ready_;
    std::optional<uint32_t> first_;
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_BLOCK_RELAY_HPP
