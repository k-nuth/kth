// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/inventory.hpp>

#include <algorithm>
#include <initializer_list>

#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const inventory::command = "inv";
uint32_t const inventory::version_minimum = version::level::minimum;
uint32_t const inventory::version_maximum = version::level::maximum;

inventory::inventory(inventory_vector::list const& values)
    : inventories_(values)
{}

inventory::inventory(inventory_vector::list&& values)
    : inventories_(std::move(values))
{}

inventory::inventory(hash_list const& hashes, type_id type) {
    inventories_.clear();
    inventories_.reserve(hashes.size());
    auto const map = [type, this](hash_digest const& hash) {
        inventories_.emplace_back(type, hash);
    };

    std::for_each(hashes.begin(), hashes.end(), map);
}

inventory::inventory(std::initializer_list<inventory_vector> const& values)
    : inventories_(values)
{}

bool inventory::operator==(inventory const& x) const {
    return (inventories_ == x.inventories_);
}

bool inventory::operator!=(inventory const& x) const {
    return !(*this == x);
}

bool inventory::is_valid() const {
    return !inventories_.empty();
}

void inventory::reset() {
    inventories_.clear();
    inventories_.shrink_to_fit();
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<inventory> inventory::from_data(byte_reader& reader, uint32_t version) {
    auto const count = reader.read_variable_little_endian();
    if ( ! count) {
        return std::unexpected(count.error());
    }
    // Guard against potential for arbitary memory allocation.
    if (*count > max_inventory) {
        return std::unexpected(error::bad_inventory_count);
    }
    inventory_vector::list inventories;
    inventories.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const inventory = inventory_vector::from_data(reader, version);
        if ( ! inventory) {
            return std::unexpected(inventory.error());
        }
        inventories.emplace_back(std::move(*inventory));
    }
    return inventory(std::move(inventories));
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk inventory::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void inventory::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

void inventory::to_hashes(hash_list& out, type_id type) const {
    out.reserve(inventories_.size());

    for (auto const& element : inventories_) {
        if (element.type() == type) {
            out.push_back(element.hash());
        }
    }

    out.shrink_to_fit();
}

void inventory::reduce(inventory_vector::list& out, type_id type) const {
    out.reserve(inventories_.size());

    for (auto const& inventory : inventories_) {
        if (inventory.type() == type) {
            out.push_back(inventory);
        }
    }

    out.shrink_to_fit();
}

size_t inventory::serialized_size(uint32_t version) const {
    return infrastructure::message::variable_uint_size(inventories_.size()) + inventories_.size() * inventory_vector::satoshi_fixed_size(version);
}

size_t inventory::count(type_id type) const {
    auto const is_type = [type](inventory_vector const& element) {
        return element.type() == type;
    };

    return count_if(inventories_.begin(), inventories_.end(), is_type);
}

inventory_vector::list& inventory::inventories() {
    return inventories_;
}

inventory_vector::list const& inventory::inventories() const {
    return inventories_;
}

void inventory::set_inventories(inventory_vector::list const& value) {
    inventories_ = value;
}

void inventory::set_inventories(inventory_vector::list&& value) {
    inventories_ = std::move(value);
}

} // namespace kth::domain::message
