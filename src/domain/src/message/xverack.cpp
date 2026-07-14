// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/xverack.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const xverack::command = "xverack";
uint32_t const xverack::version_minimum = version::level::minimum;
uint32_t const xverack::version_maximum = version::level::maximum;

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<xverack> xverack::from_data(byte_reader& /*reader*/, uint32_t /*version*/) {
    return xverack();
}

expect<void> xverack::to_data(byte_writer& /*writer*/, uint32_t /*version*/) const {
    return {};
}

size_t xverack::serialized_size(uint32_t version) const {
    return xverack::satoshi_fixed_size(version);
}

size_t xverack::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
