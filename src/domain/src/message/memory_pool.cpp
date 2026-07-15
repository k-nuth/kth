// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/memory_pool.hpp>

#include <kth/domain/message/version.hpp>
namespace kth::domain::message {

std::string const memory_pool::command = "mempool";
uint32_t const memory_pool::version_minimum = version::level::bip35;
uint32_t const memory_pool::version_maximum = version::level::maximum;

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<memory_pool> memory_pool::from_data(byte_reader& reader, uint32_t version) {
    if (version < memory_pool::version_minimum) {
        return std::unexpected(error::unsupported_version);
    }
    return memory_pool();
}

// Serialization.
//-----------------------------------------------------------------------------

} // namespace kth::domain::message
