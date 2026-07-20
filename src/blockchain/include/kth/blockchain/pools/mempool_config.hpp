// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_CONFIG_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_CONFIG_HPP

// Selects the mempool's concurrent-hashmap backend at compile time. Exactly one
// of KTH_MEMPOOL_BACKEND_CFM / KTH_MEMPOOL_BACKEND_PARLAY is defined, driven by
// the conan option `mempool_backend` -> CMake `KTH_MEMPOOL_BACKEND` (see the
// blockchain CMakeLists). The two backends exist so they can be A/B measured on
// a running node.
//
//   cfm    -> boost::concurrent_flat_map  (default)
//   parlay -> ParlayHash

#if defined(KTH_MEMPOOL_BACKEND_CFM) && defined(KTH_MEMPOOL_BACKEND_PARLAY)
    #error "Both KTH_MEMPOOL_BACKEND_CFM and KTH_MEMPOOL_BACKEND_PARLAY are defined; pick exactly one."
#elif ! defined(KTH_MEMPOOL_BACKEND_CFM) && ! defined(KTH_MEMPOOL_BACKEND_PARLAY)
    #error "No mempool backend defined. Set the conan option mempool_backend=cfm|parlay (propagates to -DKTH_MEMPOOL_BACKEND_*)."
#endif

namespace kth::blockchain {

enum class mempool_backend {
    cfm,
    parlay
};

#if defined(KTH_MEMPOOL_BACKEND_PARLAY)
inline constexpr mempool_backend active_mempool_backend = mempool_backend::parlay;
inline constexpr char const* mempool_backend_name = "parlay";
#else
inline constexpr mempool_backend active_mempool_backend = mempool_backend::cfm;
inline constexpr char const* mempool_backend_name = "cfm";
#endif

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_CONFIG_HPP
