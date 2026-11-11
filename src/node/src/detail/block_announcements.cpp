// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/block_announcements.hpp>

#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::node::detail {

std::optional<domain::message::header::list> headers_carried(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::headers::from_data(reader, version);
    if ( ! parsed) {
        return std::nullopt;
    }
    return parsed->elements();
}

std::optional<domain::message::header> header_of_compact_block(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::compact_block::from_data(reader, version);
    if ( ! parsed) {
        return std::nullopt;
    }
    return parsed->header();
}

hash_list announced_by_inventory(data_chunk const& payload, uint32_t version) {
    return announced_in_inventory(payload, version).blocks;
}

inventory_announcements announced_in_inventory(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::inventory::from_data(reader, version);
    if ( ! parsed) {
        return {};
    }

    inventory_announcements out;
    parsed->to_hashes(out.blocks, domain::message::inventory::type_id::block);
    parsed->to_hashes(out.blocks, domain::message::inventory::type_id::compact_block);
    parsed->to_hashes(out.transactions, domain::message::inventory::type_id::transaction);
    return out;
}

hash_list transactions_not_found(data_chunk const& payload, uint32_t version) {
    byte_reader reader(payload);
    auto parsed = domain::message::not_found::from_data(reader, version);
    if ( ! parsed) {
        return {};
    }
    hash_list out;
    parsed->to_hashes(out, domain::message::inventory::type_id::transaction);
    return out;
}

} // namespace kth::node::detail
