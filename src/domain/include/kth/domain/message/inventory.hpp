// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_INVENTORY_HPP
#define KTH_DOMAIN_MESSAGE_INVENTORY_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

    /// Fails with error::bad_inventory_count over max_inventory entries.
    static
    expect<inventory> create(inventory_vector::list inventories);

    /// One vector per hash, all of `type`. Same cap.
    static
    expect<inventory> create(hash_list const& hashes, type_id type);

    [[nodiscard]]
    friend bool operator==(inventory const&, inventory const&) = default;

    [[nodiscard]]
    inventory_vector::list const& inventories() const;


    static
    expect<inventory> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    /// Drop the vectors matching `pred`. Safe where a mutable accessor is not:
    /// removing entries cannot exceed a maximum.
    template <typename Predicate>
    void erase_if(Predicate pred) {
        std::erase_if(inventories_, pred);
    }

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
    inventory(inventory_vector::list inventories);

    inventory_vector::list inventories_;
};

} // namespace kth::domain::message

#endif
