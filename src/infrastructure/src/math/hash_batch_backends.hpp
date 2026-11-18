// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_HASH_BATCH_BACKENDS_HPP
#define KTH_INFRASTRUCTURE_HASH_BATCH_BACKENDS_HPP

#include <span>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

// The ways bitcoin_hash_batch can hash. Each gives out[i] = bitcoin_hash of
// messages[i]; which one runs is decided in hash_batch.cpp.

namespace kth::detail {

// One message at a time, with bitcoin_hash. What every other way has to equal.
void sha256d_batch_one_by_one(std::span<byte_span const> messages, std::span<hash_digest> out);

#if defined(ENABLE_X86_SHANI)
// Through the SHA extensions, four messages in step. Only to be called when
// the processor has them: sha256d_batch_x86_shani_usable says so.
void sha256d_batch_x86_shani(std::span<byte_span const> messages, std::span<hash_digest> out);
#endif
bool sha256d_batch_x86_shani_usable();

} // namespace kth::detail

#endif // KTH_INFRASTRUCTURE_HASH_BATCH_BACKENDS_HPP
