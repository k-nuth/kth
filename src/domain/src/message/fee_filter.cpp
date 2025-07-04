// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/fee_filter.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const fee_filter::command = "feefilter";
uint32_t const fee_filter::version_minimum = version::level::bip133;
uint32_t const fee_filter::version_maximum = version::level::bip133;

// static
constexpr
size_t fee_filter::satoshi_fixed_size(uint32_t /*version*/) {
    return sizeof(minimum_fee_);
}

// This is not a default instance so is valid.
fee_filter::fee_filter(uint64_t minimum)
    : minimum_fee_(minimum), insufficient_version_(false) {
}

// protected
fee_filter::fee_filter(uint64_t minimum, bool insufficient_version)
    : minimum_fee_(minimum), insufficient_version_(insufficient_version) {
}

//TODO(fernando): it does not compare all the data members, is it OK?
bool fee_filter::operator==(fee_filter const& x) const {
    return (minimum_fee_ == x.minimum_fee_);
}

bool fee_filter::operator!=(fee_filter const& x) const {
    return !(*this == x);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<fee_filter> fee_filter::from_data(byte_reader& reader, uint32_t version) {
    auto const minimum = reader.read_little_endian<uint64_t>();
    if ( ! minimum) {
        return make_unexpected(minimum.error());
    }
    if (version < version_minimum) {
        return make_unexpected(error::version_too_low);
    }
    auto const insufficient_version = false;
    return fee_filter(*minimum, insufficient_version);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk fee_filter::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void fee_filter::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

bool fee_filter::is_valid() const {
    // return !insufficient_version_;
    return !insufficient_version_ || (minimum_fee_ > 0);
}

// This is again a default instance so is invalid.
void fee_filter::reset() {
    insufficient_version_ = true;
    minimum_fee_ = 0;
}

size_t fee_filter::serialized_size(uint32_t version) const {
    return satoshi_fixed_size(version);
}

uint64_t fee_filter::minimum_fee() const {
    return minimum_fee_;
}

void fee_filter::set_minimum_fee(uint64_t value) {
    minimum_fee_ = value;

    // This is no longer a default instance, so is valid.
    insufficient_version_ = false;
}

} // namespace kth::domain::message
