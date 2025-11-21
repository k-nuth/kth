// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_NOT_FOUND_HPP
#define KTH_DOMAIN_MESSAGE_NOT_FOUND_HPP

#include <initializer_list>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API not_found : inventory {
public:
    using ptr = std::shared_ptr<not_found>;
    using const_ptr = std::shared_ptr<const not_found>;

    not_found() = default;
    not_found(inventory_vector::list const& values);
    not_found(inventory_vector::list&& values);
    not_found(hash_list const& hashes, type_id type);
    not_found(std::initializer_list<inventory_vector> const& values);
    not_found(inventory&& inv)
        : inventory(std::move(inv))
    {}

    bool operator==(not_found const& x) const;
    bool operator!=(not_found const& x) const;

    static
    expect<not_found> from_data(byte_reader& reader, uint32_t version);

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

};

} // namespace kth::domain::message

#endif
