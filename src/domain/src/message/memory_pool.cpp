// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/memory_pool.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const memory_pool::command = "mempool";
uint32_t const memory_pool::version_minimum = version::level::bip35;
uint32_t const memory_pool::version_maximum = version::level::maximum;

// protected
memory_pool::memory_pool(bool insufficient_version)
    : insufficient_version_(insufficient_version) {
}

bool memory_pool::is_valid() const {
    return !insufficient_version_;
}

// This is again a default instance so is invalid.
void memory_pool::reset() {
    insufficient_version_ = true;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<memory_pool> memory_pool::from_data(byte_reader& reader, uint32_t version) {
    if (version < memory_pool::version_minimum) {
        return std::unexpected(error::unsupported_version);
    }
    auto const insufficient_version = false;
    return memory_pool(insufficient_version);
}


// Serialization.
//-----------------------------------------------------------------------------

size_t memory_pool::serialized_size(uint32_t version) const {
    return memory_pool::satoshi_fixed_size(version);
}

size_t memory_pool::satoshi_fixed_size(uint32_t /*version*/) {
    return 0;
}

} // namespace kth::domain::message
