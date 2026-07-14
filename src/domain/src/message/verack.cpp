// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/verack.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const verack::command = "verack";
uint32_t const verack::version_minimum = version::level::minimum;
uint32_t const verack::version_maximum = version::level::maximum;

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<verack> verack::from_data(byte_reader& /*reader*/, uint32_t /*version*/) {
    return verack();
}

expect<void> verack::to_data(byte_writer& /*writer*/, uint32_t /*version*/) const {
    return {};
}

size_t verack::serialized_size(uint32_t version) const {
    return verack::satoshi_fixed_size(version);
}

size_t verack::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
