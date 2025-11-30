// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/filter_add.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const filter_add::command = "filteradd";
uint32_t const filter_add::version_minimum = version::level::bip37;
uint32_t const filter_add::version_maximum = version::level::maximum;

filter_add::filter_add(data_chunk const& data)
    : data_(data) {
}

filter_add::filter_add(data_chunk&& data)
    : data_(std::move(data)) {
}

bool filter_add::operator==(filter_add const& x) const {
    return (data_ == x.data_);
}

bool filter_add::operator!=(filter_add const& x) const {
    return !(*this == x);
}

bool filter_add::is_valid() const {
    return !data_.empty();
}

void filter_add::reset() {
    data_.clear();
    data_.shrink_to_fit();
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
    if (*size > max_filter_add) {
        return std::unexpected(error::invalid_filter_add);
    }

    auto data = reader.read_bytes(*size);
    if ( ! data) {
        return std::unexpected(data.error());
    }
    return filter_add(data_chunk(data->begin(), data->end()));
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk filter_add::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void filter_add::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t filter_add::serialized_size(uint32_t /*version*/) const {
    return infrastructure::message::variable_uint_size(data_.size()) + data_.size();
}

data_chunk& filter_add::data() {
    return data_;
}

data_chunk const& filter_add::data() const {
    return data_;
}

void filter_add::set_data(data_chunk const& value) {
    data_ = value;
}

void filter_add::set_data(data_chunk&& value) {
    data_ = std::move(value);
}

} // namespace kth::domain::message
