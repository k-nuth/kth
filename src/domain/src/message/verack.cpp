// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/verack.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {


// Serialization.
//-----------------------------------------------------------------------------

// static
expect<verack> verack::from_data(byte_reader& /*reader*/, uint32_t /*version*/) noexcept {
    return verack();
}

expect<void> verack::to_data(byte_writer& /*writer*/, uint32_t /*version*/) const noexcept {
    return {};
}

size_t verack::serialized_size(uint32_t version) const {
    return verack::satoshi_fixed_size(version);
}

size_t verack::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
