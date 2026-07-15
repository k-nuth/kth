// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_INVENTORY_HPP
#define KTH_DOMAIN_MESSAGE_INVENTORY_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API inventory {
    using ptr = std::shared_ptr<inventory>;
    using const_ptr = std::shared_ptr<const inventory>;
    using type_id = inventory_vector::type_id;

    inventory() = default;
    inventory(inventory_vector::list const& values);
    inventory(inventory_vector::list&& values);
    inventory(hash_list const& hashes, type_id type);
    inventory(std::initializer_list<inventory_vector> const& values);

    [[nodiscard]]
    friend bool operator==(inventory const&, inventory const&) = default;

    inventory_vector::list& inventories();

    [[nodiscard]]
    inventory_vector::list const& inventories() const;

    void set_inventories(inventory_vector::list const& value);
    void set_inventories(inventory_vector::list&& value);

    static
    expect<inventory> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    void to_hashes(hash_list& out, type_id type) const;
    void reduce(inventory_vector::list& out, type_id type) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    [[nodiscard]]
    size_t count(type_id type) const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;


private:
    inventory_vector::list inventories_;
};

} // namespace kth::domain::message

#endif
