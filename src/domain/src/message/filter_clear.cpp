// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/filter_clear.hpp>

#include <kth/domain/message/version.hpp>

namespace kth::domain::message {


// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<filter_clear> filter_clear::from_data(byte_reader& reader, uint32_t version) noexcept {
    if (version < filter_clear::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return filter_clear();
}

// Serialization.
//-----------------------------------------------------------------------------

} // namespace kth::domain::message
