// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CHAIN_MINING_INFO_CONVERSION_HPP_
#define KTH_CAPI_CHAIN_MINING_INFO_CONVERSION_HPP_

#include <cstdint>

#include <kth/capi/chain/mining_info.h>
#include <kth/capi/helpers.hpp>

#include <kth/blockchain/pools/block_template.hpp>

namespace kth {

// Both sides opt out of the byte copy, so a later call that reaches the generic
// path with either type fails to compile rather than quietly putting layout,
// padding and declaration order back into the ABI.
template <> struct forbids_struct_cast<blockchain::mining_info> : std::true_type {};
template <> struct forbids_struct_cast<kth_mining_info_t> : std::true_type {};

// `kth::blockchain::mining_info` crosses to C field by field rather than by
// memcpy.
//
// The generic conversion copies the bytes and asserts the two structs are the
// same size, which makes layout, padding and DECLARATION ORDER part of the ABI
// without saying so: three trailing `bool` fields pack into the same size in
// either order, so swapping two of them changes what every caller reads and
// nothing — not the size assertion, not the compiler — objects. Naming each
// field costs nothing at runtime and makes a rename fail to compile and a
// reorder harmless.
//
// It also removes the one real type mismatch: `chain` is an enum on the C++
// side and a `uint32_t` on the C side, so the conversion is a cast, and a cast
// is better written than implied by a memcpy.
template <>
inline
kth_mining_info_t to_c_struct<kth_mining_info_t, blockchain::mining_info>(
    blockchain::mining_info const& cpp) {

    // Zero-initialized, so a field added to the C struct before its assignment
    // is written here crosses as zero rather than as whatever was on the stack.
    kth_mining_info_t out{};
    out.blocks = cpp.blocks;
    out.difficulty = cpp.difficulty;
    out.pooled_tx = cpp.pooled_tx;
    out.chain = static_cast<uint32_t>(cpp.chain);
    out.transition_in_progress = cpp.transition_in_progress;
    out.caught_up = cpp.caught_up;
    out.fresh = cpp.fresh;
    return out;
}

} // namespace kth

#endif // KTH_CAPI_CHAIN_MINING_INFO_CONVERSION_HPP_
