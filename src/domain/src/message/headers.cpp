// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/headers.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <istream>
#include <utility>

#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/limits.hpp>
namespace kth::domain::message {

std::string const headers::command = "headers";
uint32_t const headers::version_minimum = version::level::headers;
uint32_t const headers::version_maximum = version::level::maximum;

// Uses headers copy assignment.
headers::headers(header::list const& values)
    : elements_(values) {
}

headers::headers(header::list&& values)
    : elements_(std::move(values)) {
}

headers::headers(std::initializer_list<header> const& values)
    : elements_(values) {
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<headers> headers::from_data(byte_reader& reader, uint32_t version) {
    auto const count = reader.read_variable_little_endian();
    if ( ! count) {
        return std::unexpected(count.error());
    }
    if (*count > max_get_headers) {
        return std::unexpected(error::version_too_new);
    }
    header::list elements;
    elements.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto element = header::from_data(reader, version);
        if ( ! element) {
            return std::unexpected(element.error());
        }
        elements.push_back(std::move(*element));
    }

    if (version < headers::version_minimum) {
        return std::unexpected(error::version_too_new);
    }
    return headers(std::move(elements));
}

// Serialization.
//-----------------------------------------------------------------------------



bool headers::is_sequential() const {
    if (elements_.empty()) {
        return true;
    }

    auto previous = chain::hash(elements_.front());

    for (auto it = elements_.begin() + 1; it != elements_.end(); ++it) {
        if (it->previous_block_hash() != previous) {
            return false;
        }

        previous = chain::hash(*it);
    }

    return true;
}

void headers::to_hashes(hash_list& out) const {
    out.clear();
    out.reserve(elements_.size());
    auto const map = [&out](header const& header) {
        out.push_back(chain::hash(header));
    };

    std::for_each(elements_.begin(), elements_.end(), map);
}

void headers::to_inventory(inventory_vector::list& out,
                           inventory::type_id type) const {
    out.clear();
    out.reserve(elements_.size());
    auto const map = [&out, type](header const& header) {
        out.emplace_back(type, chain::hash(header));
    };

    std::for_each(elements_.begin(), elements_.end(), map);
}

size_t headers::serialized_size(uint32_t version) const {
    return infrastructure::message::variable_uint_size(elements_.size()) +
           (elements_.size() * header::satoshi_fixed_size(version));
}

header::list& headers::elements() {
    return elements_;
}

header::list const& headers::elements() const {
    return elements_;
}

void headers::set_elements(header::list const& values) {
    elements_ = values;
}

void headers::set_elements(header::list&& values) {
    elements_ = std::move(values);
}

expect<void> headers::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_variable_little_endian(elements_.size()); ! r) return r;

        for (auto const& element : elements_) {
            if (auto r = element.to_data(writer, version); ! r) return r;
        }
        return {};
}

} // namespace kth::domain::message
