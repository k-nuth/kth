// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_HASH_BATCH_HPP
#define KTH_INFRASTRUCTURE_HASH_BATCH_HPP

#include <cstddef>
#include <span>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// The bitcoin hash (double SHA-256) of many messages at once.
//
// out[i] is bitcoin_hash(messages[i]) for every i, whatever the lengths: the
// result is the same as hashing them one by one. What changes is how. A
// processor has room to run several SHA-256 computations side by side, which
// one message at a time never uses, so given the messages together this
// hashes several of them in step.
//
// `out` must have room for one hash per message, and must not overlap them.
KI_API void bitcoin_hash_batch(std::span<byte_span const> messages, std::span<hash_digest> out);

// How bitcoin_hash_batch hashes on this machine, for logs and tests.
KI_API std::string_view bitcoin_hash_batch_implementation();

} // namespace kth

#endif // KTH_INFRASTRUCTURE_HASH_BATCH_HPP
