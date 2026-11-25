// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/send_headers.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {


// Serialization.
//-----------------------------------------------------------------------------

// static
expect<send_headers> send_headers::from_data(byte_reader& reader, uint32_t version) noexcept {
    if (version < send_headers::version_minimum) {
        return std::unexpected(error::version_too_low);
    }
    return send_headers();
}

expect<void> send_headers::to_data(byte_writer& /*writer*/, uint32_t /*version*/) const noexcept {
    return {};
}

} // namespace kth::domain::message
