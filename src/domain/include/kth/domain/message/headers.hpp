// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_HEADERS_HPP
#define KTH_DOMAIN_MESSAGE_HEADERS_HPP

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API headers {
    using ptr = std::shared_ptr<headers>;
    using const_ptr = std::shared_ptr<const headers>;

    headers() = default;

    /// Fails with error::invalid_headers_count over max_get_headers entries.
    static
    expect<headers> create(header::list elements);
    headers(std::initializer_list<header> const& values);

    [[nodiscard]]
    friend bool operator==(headers const&, headers const&) = default;

    [[nodiscard]]
    header::list const& elements() const;


    [[nodiscard]]
    bool is_sequential() const;

    void to_hashes(hash_list& out) const;
    void to_inventory(inventory_vector::list& out, inventory::type_id type) const;

    static
    expect<headers> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;


private:
    headers(header::list elements);

    header::list elements_;
};

} // namespace kth::domain::message

#endif
