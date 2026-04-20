// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_VM_FUNCTION_TABLE_H_
#define KTH_CAPI_VM_FUNCTION_TABLE_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @return Owned `kth_function_table_mut_t`. Caller must release with `kth_vm_function_table_destruct`. */
KTH_EXPORT KTH_OWNED
kth_function_table_mut_t kth_vm_function_table_construct_default(void);

/** No-op if `map` is null. */
KTH_EXPORT
void kth_vm_function_table_destruct(kth_function_table_mut_t map);

KTH_EXPORT
kth_size_t kth_vm_function_table_count(kth_function_table_const_t map);

/** Non-zero iff `key` is present. */
KTH_EXPORT
kth_bool_t kth_vm_function_table_contains(kth_function_table_const_t map, uint8_t const* key, kth_size_t key_n);

/** @return Owned byte buffer (value bytes, length in `out_value_size`), or NULL if `key` is absent. Release non-NULL results with `kth_core_destruct_array`. */
KTH_EXPORT KTH_OWNED
uint8_t* kth_vm_function_table_at(kth_function_table_const_t map, uint8_t const* key, kth_size_t key_n, kth_size_t* out_value_size);

/** Insert or overwrite the `key` → `value` mapping. */
KTH_EXPORT
void kth_vm_function_table_insert(kth_function_table_mut_t map, uint8_t const* key, kth_size_t key_n, uint8_t const* value, kth_size_t value_n);

/** @return Non-zero iff a mapping was erased. */
KTH_EXPORT
kth_bool_t kth_vm_function_table_erase(kth_function_table_mut_t map, uint8_t const* key, kth_size_t key_n);

/** Iterate entries by position in an implementation-defined order (`unordered_flat_map` iteration order is stable across reads but not defined w.r.t. insertion). Returns owned buffers for both the key and the value; release them with `kth_core_destruct_array`. No-op and leaves outputs untouched if `index >= count`. */
KTH_EXPORT
void kth_vm_function_table_nth(kth_function_table_const_t map, kth_size_t index,
               KTH_OUT_OWNED uint8_t** out_key, kth_size_t* out_key_size,
               KTH_OUT_OWNED uint8_t** out_value, kth_size_t* out_value_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_VM_FUNCTION_TABLE_H_ */
