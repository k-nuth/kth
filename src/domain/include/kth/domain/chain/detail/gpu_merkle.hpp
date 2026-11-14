// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_DETAIL_GPU_MERKLE_HPP
#define KTH_DOMAIN_CHAIN_DETAIL_GPU_MERKLE_HPP

#include <optional>

#include <kth/infrastructure/math/hash.hpp>

#if defined(KTH_SIGVERIFY_TURBO_BATCH)
#include <secp256k1_turbo/gpu_sha256.hpp>
#endif

namespace kth::domain::chain::detail {

/// The merkle root of `hashes`, when the GPU hashed the tree: a build with a
/// GPU runtime, a device present, and enough hashes for a launch to pay.
/// Otherwise nullopt, and the caller hashes it on the CPU; `mutated` is then
/// left alone.
///
/// `mutated` is whether two equal hashes were paired as siblings at any level.
inline
std::optional<hash_digest> gpu_merkle_root(hash_list const& hashes, bool& mutated) {
#if defined(KTH_SIGVERIFY_TURBO_BATCH)
    if ( ! hashes.empty() && secp256k1_turbo::gpu_sha256::worth_it(hashes.size())) {
        // A hash_list is its hashes one after another: 32 bytes each.
        static_assert(sizeof(hash_digest) == 32);
        hash_digest root;
        bool equal_siblings = false;
        if (secp256k1_turbo::gpu_sha256::merkle_root(root.data(), hashes.front().data(), hashes.size(),
                &equal_siblings)) {
            mutated = equal_siblings;
            return root;
        }
    }
#else
    (void)hashes;
    (void)mutated;
#endif
    return std::nullopt;
}

} // namespace kth::domain::chain::detail

#endif // KTH_DOMAIN_CHAIN_DETAIL_GPU_MERKLE_HPP
