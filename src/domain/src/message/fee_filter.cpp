// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/fee_filter.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/error.hpp>
namespace kth::domain::message {

std::string const fee_filter::command = "feefilter";
uint32_t const fee_filter::version_minimum = version::level::bip133;
uint32_t const fee_filter::version_maximum = version::level::bip133;

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<fee_filter> fee_filter::from_data(byte_reader& reader, uint32_t version) {
    auto const minimum = reader.read_little_endian<uint64_t>();
    if ( ! minimum) {
        return std::unexpected(minimum.error());
    }
    if (version < version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return fee_filter(*minimum);
}

// Serialization.
//-----------------------------------------------------------------------------

// This is again a default instance so is invalid.
expect<void> fee_filter::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_little_endian<uint64_t>(minimum_fee_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
