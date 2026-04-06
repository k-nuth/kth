// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/conversions.hpp>

kth::domain::chain::chain_state const& kth_chain_chain_state_const_cpp(kth_chain_state_const_t o) {
    return *static_cast<kth::domain::chain::chain_state const*>(o);
}
