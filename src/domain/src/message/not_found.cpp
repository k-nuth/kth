// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/not_found.hpp>

#include <initializer_list>

#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::message {

std::string const not_found::command = "notfound";
uint32_t const not_found::version_minimum = version::level::bip37;
uint32_t const not_found::version_maximum = version::level::maximum;

// static
expect<not_found> not_found::create(inventory_vector::list inventories) {
    auto inv = inventory::create(std::move(inventories));
    if ( ! inv) {
        return std::unexpected(inv.error());
    }
    return not_found(std::move(*inv));
}

// static
expect<not_found> not_found::create(hash_list const& hashes, type_id type) {
    auto inv = inventory::create(hashes, type);
    if ( ! inv) {
        return std::unexpected(inv.error());
    }
    return not_found(std::move(*inv));
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<not_found> not_found::from_data(byte_reader& reader, uint32_t version) {
    auto inv = inventory::from_data(reader, version);
    if ( ! inv) {
        return std::unexpected(inv.error());
    }

    if (version < not_found::version_minimum) {
        return std::unexpected(error::unsupported_version);
    }

    return not_found(std::move(*inv));
}

} // namespace kth::domain::message
