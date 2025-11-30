// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_HEADERS_HPP
#define KTH_DOMAIN_MESSAGE_HEADERS_HPP

#include <cstdint>
#include <initializer_list>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/message/header.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API headers {
    using ptr = std::shared_ptr<headers>;
    using const_ptr = std::shared_ptr<const headers>;

    headers() = default;
    headers(header::list const& values);
    headers(header::list&& values);
    headers(std::initializer_list<header> const& values);

    bool operator==(headers const& x) const;
    bool operator!=(headers const& x) const;

    header::list& elements();

    [[nodiscard]]
    header::list const& elements() const;

    void set_elements(header::list const& values);
    void set_elements(header::list&& values);

    [[nodiscard]]
    bool is_sequential() const;

    void to_hashes(hash_list& out) const;
    void to_inventory(inventory_vector::list& out, inventory::type_id type) const;

    static
    expect<headers> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t version, W& sink) const {
        sink.write_variable_little_endian(elements_.size());

        for (auto const& element : elements_) {
            element.to_data(version, sink);
        }
    }

    //void to_data(uint32_t version, writer& sink) const;
    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;


private:
    header::list elements_;
};

} // namespace kth::domain::message

#endif
