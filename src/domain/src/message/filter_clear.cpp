// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/filter_clear.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const filter_clear::command = "filterclear";
uint32_t const filter_clear::version_minimum = version::level::bip37;
uint32_t const filter_clear::version_maximum = version::level::maximum;

// protected
filter_clear::filter_clear(bool insufficient_version)
    : insufficient_version_(insufficient_version) {
}

bool filter_clear::is_valid() const {
    return !insufficient_version_;
}

// This is again a default instance so is invalid.
void filter_clear::reset() {
    insufficient_version_ = true;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<filter_clear> filter_clear::from_data(byte_reader& reader, uint32_t version) {
    auto const insufficient_version = false;
    if (version < filter_clear::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return filter_clear(insufficient_version);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk filter_clear::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void filter_clear::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t filter_clear::serialized_size(uint32_t version) const {
    return filter_clear::satoshi_fixed_size(version);
}

size_t filter_clear::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
