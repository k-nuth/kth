// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/domain/chain/abla.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/abla.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Setters

void kth_domain_chain_abla_set_max(kth_abla_config_mut_t cfg) {
    KTH_PRECONDITION(cfg != nullptr);
    auto& cfg_cpp = kth::cpp_ref<kth::domain::chain::abla::config>(cfg);
    kth::domain::chain::abla::set_max(cfg_cpp);
}


// Static utilities

kth_abla_config_mut_t kth_domain_chain_abla_default_config(uint64_t default_block_size, kth_bool_t fixed_size) {
    auto const fixed_size_cpp = kth::int_to_bool(fixed_size);
    return kth::leak(kth::domain::chain::abla::default_config(default_block_size, fixed_size_cpp));
}

kth_abla_config_validity_t kth_domain_chain_abla_validate_config(kth_abla_config_const_t cfg) {
    KTH_PRECONDITION(cfg != nullptr);
    auto const& cfg_cpp = kth::cpp_ref<kth::domain::chain::abla::config>(cfg);
    return static_cast<kth_abla_config_validity_t>(kth::domain::chain::abla::validate(cfg_cpp));
}

uint64_t kth_domain_chain_abla_block_size_limit(kth_abla_state_const_t st) {
    KTH_PRECONDITION(st != nullptr);
    auto const& st_cpp = kth::cpp_ref<kth::domain::chain::abla::state>(st);
    return kth::domain::chain::abla::block_size_limit(st_cpp);
}

kth_abla_state_mut_t kth_domain_chain_abla_next(kth_abla_state_const_t st, kth_abla_config_const_t cfg, uint64_t next_block_size) {
    KTH_PRECONDITION(st != nullptr);
    KTH_PRECONDITION(cfg != nullptr);
    auto const& st_cpp = kth::cpp_ref<kth::domain::chain::abla::state>(st);
    auto const& cfg_cpp = kth::cpp_ref<kth::domain::chain::abla::config>(cfg);
    auto result = kth::domain::chain::abla::next(st_cpp, cfg_cpp, next_block_size);
    return result.has_value() ? kth::leak(std::move(*result)) : nullptr;
}

kth_abla_state_mut_t kth_domain_chain_abla_lookahead(kth_abla_state_const_t st, kth_abla_config_const_t cfg, kth_size_t count) {
    KTH_PRECONDITION(st != nullptr);
    KTH_PRECONDITION(cfg != nullptr);
    auto const& st_cpp = kth::cpp_ref<kth::domain::chain::abla::state>(st);
    auto const& cfg_cpp = kth::cpp_ref<kth::domain::chain::abla::config>(cfg);
    auto const count_cpp = kth::sz(count);
    auto result = kth::domain::chain::abla::lookahead(st_cpp, cfg_cpp, count_cpp);
    return result.has_value() ? kth::leak(std::move(*result)) : nullptr;
}

kth_abla_state_validity_t kth_domain_chain_abla_validate_state(kth_abla_state_const_t st, kth_abla_config_const_t cfg) {
    KTH_PRECONDITION(st != nullptr);
    KTH_PRECONDITION(cfg != nullptr);
    auto const& st_cpp = kth::cpp_ref<kth::domain::chain::abla::state>(st);
    auto const& cfg_cpp = kth::cpp_ref<kth::domain::chain::abla::config>(cfg);
    return static_cast<kth_abla_state_validity_t>(kth::domain::chain::abla::validate(st_cpp, cfg_cpp));
}

} // extern "C"
