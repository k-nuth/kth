// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/filter_clear.hpp>

#include <kth/domain/message/version.hpp>

namespace kth::domain::message {

std::string const filter_clear::command = "filterclear";
uint32_t const filter_clear::version_minimum = version::level::bip37;
uint32_t const filter_clear::version_maximum = version::level::maximum;

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<filter_clear> filter_clear::from_data(byte_reader& reader, uint32_t version) {
    if (version < filter_clear::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return filter_clear();
}

// Serialization.
//-----------------------------------------------------------------------------

} // namespace kth::domain::message
