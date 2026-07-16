// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_data.hpp>

#include <algorithm>
#include <initializer_list>

#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::message {

std::string const get_data::command = "getdata";
uint32_t const get_data::version_minimum = version::level::minimum;
uint32_t const get_data::version_maximum = version::level::maximum;

// static
expect<get_data> get_data::create(inventory_vector::list inventories) {
    auto inv = inventory::create(std::move(inventories));
    if ( ! inv) {
        return std::unexpected(inv.error());
    }
    return get_data(std::move(*inv));
}

// static
expect<get_data> get_data::create(hash_list const& hashes, type_id type) {
    auto inv = inventory::create(hashes, type);
    if ( ! inv) {
        return std::unexpected(inv.error());
    }
    return get_data(std::move(*inv));
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<get_data> get_data::from_data(byte_reader& reader, uint32_t version) {
    if (version < version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    auto inv = inventory::from_data(reader, version);
    if ( ! inv) {
        return std::unexpected(inv.error());
    }
    return get_data(std::move(*inv));
}

} // namespace kth::domain::message
