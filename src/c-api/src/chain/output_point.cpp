// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/output_point.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

// Conversion functions
kth::domain::chain::output_point& kth_chain_output_point_cpp(kth_output_point_mut_t o) {
    return *static_cast<kth::domain::chain::output_point*>(o);
}
kth::domain::chain::output_point const& kth_chain_output_point_const_cpp(kth_output_point_const_t o) {
    return *static_cast<kth::domain::chain::output_point const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_output_point_mut_t kth_chain_output_point_construct_default() {
    return new kth::domain::chain::output_point();
}

kth_output_point_mut_t kth_chain_output_point_construct(uint8_t const* hash, uint32_t index) {
    return new kth::domain::chain::output_point(kth::hash_to_cpp(hash), index);
}

void kth_chain_output_point_destruct(kth_output_point_mut_t output_point) {
    if (output_point == nullptr) return;
    delete &kth_chain_output_point_cpp(output_point);
}

kth_bool_t kth_chain_output_point_is_mature(kth_output_point_const_t output_point, kth_size_t height) {
    return kth::bool_to_int(kth_chain_output_point_const_cpp(output_point).is_mature(height));
}

kth_hash_t kth_chain_output_point_hash(kth_output_point_const_t output_point) {
    auto hash_cpp = kth_chain_output_point_const_cpp(output_point).hash();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_output_point_set_hash(kth_output_point_mut_t output_point, uint8_t const* hash) {
    kth_chain_output_point_cpp(output_point).set_hash(kth::hash_to_cpp(hash));
}

uint32_t kth_chain_output_point_index(kth_output_point_const_t output_point) {
    return kth_chain_output_point_const_cpp(output_point).index();
}

void kth_chain_output_point_set_index(kth_output_point_mut_t output_point, uint32_t value) {
    kth_chain_output_point_cpp(output_point).set_index(value);
}

kth_bool_t kth_chain_output_point_is_null(kth_output_point_const_t output_point) {
    return kth::bool_to_int(kth_chain_output_point_const_cpp(output_point).is_null());
}

kth_bool_t kth_chain_output_point_is_valid(kth_output_point_const_t output_point) {
    return kth::bool_to_int(kth_chain_output_point_const_cpp(output_point).is_valid());
}

uint64_t kth_chain_output_point_checksum(kth_output_point_const_t output_point) {
    return kth_chain_output_point_const_cpp(output_point).checksum();
}

uint8_t const* kth_chain_output_point_to_data(kth_output_point_const_t output_point, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_output_point_const_cpp(output_point).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_output_point_serialized_size(kth_output_point_const_t output_point, kth_bool_t wire) {
    return kth_chain_output_point_const_cpp(output_point).serialized_size(kth::int_to_bool(wire));
}

} // extern "C"
