// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/filter_add.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/limits.hpp>
namespace kth::domain::message {

std::string const filter_add::command = "filteradd";
uint32_t const filter_add::version_minimum = version::level::bip37;
uint32_t const filter_add::version_maximum = version::level::maximum;

filter_add::filter_add(data_chunk data)
    : data_(std::move(data))
{}

// static
expect<filter_add> filter_add::create(data_chunk data) {
    // BIP37 caps filteradd; anything above it cannot be encoded.
    if (data.size() > max_filter_add) {
        return std::unexpected(error::invalid_filter_add);
    }
    return filter_add(std::move(data));
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<filter_add> filter_add::from_data(byte_reader& reader, uint32_t version) {
    if (version < version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    
    auto const size = reader.read_size_little_endian();
    if ( ! size) {
        return std::unexpected(size.error());
    }
    // Checked before reading, so an oversized claim cannot make us allocate.
    if (*size > max_filter_add) {
        return std::unexpected(error::invalid_filter_add);
    }

    auto data = reader.read_bytes(*size);
    if ( ! data) {
        return std::unexpected(data.error());
    }
    return create(data_chunk(data->begin(), data->end()));
}

// Serialization.
//-----------------------------------------------------------------------------



size_t filter_add::serialized_size(uint32_t /*version*/) const {
    return infrastructure::message::variable_uint_size(data_.size()) + data_.size();
}

data_chunk const& filter_add::data() const {
    return data_;
}

expect<void> filter_add::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_variable_little_endian(data_.size()); ! r) return r;
        if (auto r = writer.write_bytes(data_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
