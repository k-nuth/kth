// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONSENSUS_CONSENSUS_HPP
#define KTH_CONSENSUS_CONSENSUS_HPP

#include <cstddef>
#include <kth/consensus/define.hpp>
#include <kth/consensus/export.hpp>
#include "script/script_error.h"

namespace kth::consensus {

// These are not published in the public header but are exposed here for test.
KC_API verify_result_type script_error_to_verify_result(ScriptError code);

KC_API char const* version();

} // namespace kth::consensus

#endif
