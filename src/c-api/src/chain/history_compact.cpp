// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/history.hpp>

#include <kth/capi/chain/history_compact.h>

kth::domain::chain::history_compact const& kth_chain_history_compact_const_cpp(kth_history_compact_t o) {
    return *static_cast<kth::domain::chain::history_compact const*>(o);
}
kth::domain::chain::history_compact const& kth_chain_history_compact_const_cpp(kth_history_compact_const_t o) {
    return *static_cast<kth::domain::chain::history_compact const*>(o);
}
kth::domain::chain::history_compact& kth_chain_history_compact_cpp(kth_history_compact_t o) {
    return *static_cast<kth::domain::chain::history_compact*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_point_kind_t kth_chain_history_compact_get_point_kind(kth_history_compact_t history) {
    return static_cast<kth_point_kind_t>(kth_chain_history_compact_const_cpp(history).kind);
}

kth_point_t kth_chain_history_compact_get_point(kth_history_compact_t history) {
    return &kth_chain_history_compact_cpp(history).point;
}

uint32_t kth_chain_history_compact_get_height(kth_history_compact_t history) {
    return kth_chain_history_compact_const_cpp(history).height;
}

uint64_t kth_chain_history_compact_get_value_or_previous_checksum(kth_history_compact_t history) {
    auto const& history_const_cpp = kth_chain_history_compact_const_cpp(history);
    return history_const_cpp.value;
}

} // extern "C"
