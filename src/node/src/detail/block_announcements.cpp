// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/block_announcements.hpp>

#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::node::detail {

hash_list announced_by_headers(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::headers::from_data(reader, version);
    if ( ! parsed) {
        return {};
    }

    hash_list hashes;
    hashes.reserve(parsed->elements().size());
    for (auto const& header : parsed->elements()) {
        hashes.push_back(kth::domain::chain::hash(header));
    }
    return hashes;
}

hash_list announced_by_compact_block(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::compact_block::from_data(reader, version);
    if ( ! parsed) {
        return {};
    }
    return {kth::domain::chain::hash(parsed->header())};
}

hash_list announced_by_inventory(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::inventory::from_data(reader, version);
    if ( ! parsed) {
        return {};
    }

    hash_list hashes;
    parsed->to_hashes(hashes, domain::message::inventory::type_id::block);
    parsed->to_hashes(hashes, domain::message::inventory::type_id::compact_block);
    return hashes;
}

} // namespace kth::node::detail
