// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_MACHINE_SCRIPT_VERSION_HPP
#define KTH_INFRASTUCTURE_MACHINE_SCRIPT_VERSION_HPP

#include <cstdint>

#if ! defined(KTH_CURRENCY_BCH)

namespace kth::infrastructure::machine {

/// Script versions (bip141).
enum class script_version {
    /// Defined by bip141.
    zero,

    /// All reserved script versions (1..16).
    reserved,

    /// All unversioned scripts.
    unversioned
};

} // namespace kth::infrastructure::machine

#endif // ! KTH_CURRENCY_BCH

#endif // KTH_INFRASTUCTURE_MACHINE_SCRIPT_VERSION_HPP
