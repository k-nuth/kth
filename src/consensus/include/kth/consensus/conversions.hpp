// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONSENSUS_CONVERSIONS_HPP
#define KTH_CONSENSUS_CONVERSIONS_HPP

#include <cstddef>
#include <kth/consensus/define.hpp>
#include <kth/consensus/export.hpp>

namespace kth::consensus {

KC_API unsigned int verify_flags_to_script_flags(unsigned int flags);

} // namespace kth::consensus

#endif // KTH_CONSENSUS_CONVERSIONS_HPP
