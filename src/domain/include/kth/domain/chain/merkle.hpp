// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_MERKLE_HPP
#define KTH_DOMAIN_CHAIN_MERKLE_HPP

#include <cstddef>
#include <span>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::chain {

// The Merkle tree of a block's transactions, as Bitcoin builds it: each level
// pairs the hashes of the one below and hashes each pair twice with SHA-256,
// and the last hash of a level with an odd count is paired with itself.
//
// Every function takes its hashes by value and uses them as its working space:
// a level is hashed in place, over the one below, so nothing else is allocated
// or copied. A caller that still needs its hashes passes a copy; one that does
// not moves them in.

// The root of the tree. No hashes give null_hash, and one hash is its own root.
// A large tree is hashed on the GPU when the build has one and a device is
// present.
[[nodiscard]]
KD_API
hash_digest merkle_root(hash_list hashes);

// The same, and whether two equal hashes were paired as siblings at any level.
// The last hash of an odd level paired with itself does not count. A tree with
// such a pair has the root of a shorter list of transactions (CVE-2012-2459).
[[nodiscard]]
KD_API
hash_digest merkle_root(hash_list hashes, bool& mutated);

// The Merkle branch of the hash at `index`: its sibling at each level, from the
// transactions up to just below the root. The sibling of a last hash with no
// partner is that hash itself. One hash has an empty branch, and so has an
// index past the last hash.
[[nodiscard]]
KD_API
hash_list merkle_branch(hash_list hashes, size_t index);

// The branches of several hashes, from one pass over the tree: the result has
// one branch for each index, in the order given.
[[nodiscard]]
KD_API
std::vector<hash_list> merkle_branches(hash_list hashes, std::span<size_t const> indexes);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_MERKLE_HPP
