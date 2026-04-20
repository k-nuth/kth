// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/function_table.h>

#include <cstring>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = boost::unordered_flat_map<kth::data_chunk, kth::data_chunk>;
using byte_vec = kth::data_chunk;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

kth_function_table_mut_t kth_vm_function_table_construct_default(void) {
    return kth::leak<cpp_t>();
}

void kth_vm_function_table_destruct(kth_function_table_mut_t map) {
    kth::del<cpp_t>(map);
}

kth_size_t kth_vm_function_table_count(kth_function_table_const_t map) {
    KTH_PRECONDITION(map != nullptr);
    return kth::cpp_ref<cpp_t>(map).size();
}

kth_bool_t kth_vm_function_table_contains(kth_function_table_const_t map, uint8_t const* key, kth_size_t key_n) {
    KTH_PRECONDITION(map != nullptr);
    KTH_PRECONDITION(key != nullptr || key_n == 0);
    auto const k = key_n != 0 ? byte_vec(key, key + key_n) : byte_vec{};
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(map).contains(k));
}

uint8_t* kth_vm_function_table_at(kth_function_table_const_t map, uint8_t const* key, kth_size_t key_n, kth_size_t* out_value_size) {
    KTH_PRECONDITION(map != nullptr);
    KTH_PRECONDITION(key != nullptr || key_n == 0);
    KTH_PRECONDITION(out_value_size != nullptr);
    auto const k = key_n != 0 ? byte_vec(key, key + key_n) : byte_vec{};
    auto const& m = kth::cpp_ref<cpp_t>(map);
    auto const it = m.find(k);
    if (it == m.end()) {
        *out_value_size = 0;
        return nullptr;
    }
    // Guard the empty-value path: `create_c_array` would route
    // through `malloc(0)`, which is implementation-defined — some
    // platforms return NULL, making a present-but-empty value
    // indistinguishable from the absent-key signal above. Allocate
    // 1 byte directly for that case so the returned pointer is
    // always non-NULL for present keys.
    if (it->second.empty()) {
        *out_value_size = 0;
        return kth::mnew<uint8_t>(1);
    }
    return kth::create_c_array(it->second, *out_value_size);
}

void kth_vm_function_table_insert(kth_function_table_mut_t map, uint8_t const* key, kth_size_t key_n, uint8_t const* value, kth_size_t value_n) {
    KTH_PRECONDITION(map != nullptr);
    KTH_PRECONDITION(key != nullptr || key_n == 0);
    KTH_PRECONDITION(value != nullptr || value_n == 0);
    auto k = key_n != 0 ? byte_vec(key, key + key_n) : byte_vec{};
    auto v = value_n != 0 ? byte_vec(value, value + value_n) : byte_vec{};
    kth::cpp_ref<cpp_t>(map).insert_or_assign(std::move(k), std::move(v));
}

kth_bool_t kth_vm_function_table_erase(kth_function_table_mut_t map, uint8_t const* key, kth_size_t key_n) {
    KTH_PRECONDITION(map != nullptr);
    KTH_PRECONDITION(key != nullptr || key_n == 0);
    auto const k = key_n != 0 ? byte_vec(key, key + key_n) : byte_vec{};
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(map).erase(k) != 0);
}

void kth_vm_function_table_nth(kth_function_table_const_t map, kth_size_t index,
               uint8_t** out_key, kth_size_t* out_key_size,
               uint8_t** out_value, kth_size_t* out_value_size) {
    KTH_PRECONDITION(map != nullptr);
    KTH_PRECONDITION(out_key != nullptr && out_key_size != nullptr);
    KTH_PRECONDITION(out_value != nullptr && out_value_size != nullptr);
    auto const& m = kth::cpp_ref<cpp_t>(map);
    if (index >= m.size()) return;
    auto it = m.begin();
    std::advance(it, index);
    *out_key = kth::create_c_array(it->first, *out_key_size);
    *out_value = kth::create_c_array(it->second, *out_value_size);
}

} // extern "C"
