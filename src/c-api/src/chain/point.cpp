// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/point.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

// Conversion functions
kth::domain::chain::point& kth_chain_point_cpp(kth_point_mut_t o) {
    return *static_cast<kth::domain::chain::point*>(o);
}
kth::domain::chain::point const& kth_chain_point_const_cpp(kth_point_const_t o) {
    return *static_cast<kth::domain::chain::point const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_point_mut_t kth_chain_point_construct_default() {
    return new kth::domain::chain::point();
}

kth_point_mut_t kth_chain_point_construct(uint8_t const* hash, uint32_t index) {
    return new kth::domain::chain::point(kth::hash_to_cpp(hash), index);
}

void kth_chain_point_destruct(kth_point_mut_t point) {
    if (point == nullptr) return;
    delete &kth_chain_point_cpp(point);
}

kth_error_code_t kth_chain_point_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_point_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::point::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

kth_bool_t kth_chain_point_is_valid(kth_point_const_t point) {
    return kth::bool_to_int(kth_chain_point_const_cpp(point).is_valid());
}

uint8_t const* kth_chain_point_to_data(kth_point_const_t point, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_point_const_cpp(point).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_point_satoshi_fixed_size() {
    return kth::domain::chain::point::satoshi_fixed_size();
}

kth_size_t kth_chain_point_serialized_size(kth_point_const_t point, kth_bool_t wire) {
    return kth_chain_point_const_cpp(point).serialized_size(kth::int_to_bool(wire));
}

kth_hash_t kth_chain_point_hash(kth_point_const_t point) {
    auto hash_cpp = kth_chain_point_const_cpp(point).hash();
    return kth::to_hash_t(hash_cpp);
}

void kth_chain_point_hash_out(kth_point_const_t point, kth_hash_t* out_hash) {
    auto hash_cpp = kth_chain_point_const_cpp(point).hash();
    kth::copy_c_hash(hash_cpp, out_hash);
}

void kth_chain_point_set_hash(kth_point_mut_t point, uint8_t const* hash) {
    kth_chain_point_cpp(point).set_hash(kth::hash_to_cpp(hash));
}

uint32_t kth_chain_point_index(kth_point_const_t point) {
    return kth_chain_point_const_cpp(point).index();
}

void kth_chain_point_set_index(kth_point_mut_t point, uint32_t value) {
    kth_chain_point_cpp(point).set_index(value);
}

uint64_t kth_chain_point_checksum(kth_point_const_t point) {
    return kth_chain_point_const_cpp(point).checksum();
}

kth_bool_t kth_chain_point_is_null(kth_point_const_t point) {
    return kth::bool_to_int(kth_chain_point_const_cpp(point).is_null());
}

void kth_chain_point_reset(kth_point_mut_t point) {
    kth_chain_point_cpp(point).reset();
}

} // extern "C"
