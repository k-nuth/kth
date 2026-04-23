// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DATA_STACK_H_
#define KTH_CAPI_DATA_STACK_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// `data_stack` is the Bitcoin script runtime stack: a list of
// variable-length byte buffers. Each element is an owned `data_chunk`
// (kept inside the list). Hand-written — the generator only emits
// list modules for fixed-size or opaque-handle elements today; a
// list of raw byte buffers needs a bespoke `push_back(ptr, len)` /
// `nth(index, *out_size)` surface.

/** @return Owned `kth_data_stack_mut_t`. Caller must release with `kth_core_data_stack_destruct`. */
KTH_EXPORT KTH_OWNED
kth_data_stack_mut_t kth_core_data_stack_construct_default(void);

/** No-op if `list` is null. */
KTH_EXPORT
void kth_core_data_stack_destruct(kth_data_stack_mut_t list);

/** @return Owned `kth_data_stack_mut_t`. Caller must release with `kth_core_data_stack_destruct`. */
KTH_EXPORT KTH_OWNED
kth_data_stack_mut_t kth_core_data_stack_copy(kth_data_stack_const_t list);

KTH_EXPORT
kth_size_t kth_core_data_stack_count(kth_data_stack_const_t list);

/**
 * Append a new byte buffer to the list. The buffer is copied by value
 * into the list — the caller retains ownership of `data`.
 * `data == NULL` is allowed only when `n == 0` (produces an empty
 * element).
 */
KTH_EXPORT
void kth_core_data_stack_push_back(kth_data_stack_mut_t list, uint8_t const* data, kth_size_t n);

/**
 * Borrowed view into the n-th element. The returned pointer refers
 * to memory owned by `list` and is invalidated by any mutation of
 * the list. `*out_size` is always set to the element's byte count
 * — use that to decide whether to dereference the pointer, since
 * `std::vector::data()` on an empty element may legally return
 * `NULL` on some platforms (e.g. MSVC).
 *
 * @param list Must be non-null.
 * @param n    Must be strictly less than `count(list)`.
 * @param out_size Must be non-null; receives the element length.
 * @return Borrowed pointer into the element's bytes, or `NULL` when
 *         the element is empty. Do not free.
 */
KTH_EXPORT
uint8_t const* kth_core_data_stack_nth(kth_data_stack_const_t list, kth_size_t n, kth_size_t* out_size);

/**
 * Owned copy of the n-th element. Caller releases with
 * `kth_core_destruct_array`. `*out_size` is set to the element's
 * byte count regardless of whether the copy was needed.
 */
KTH_EXPORT KTH_OWNED
uint8_t* kth_core_data_stack_nth_copy(kth_data_stack_const_t list, kth_size_t n, kth_size_t* out_size);

/** Remove the n-th element. `n` must be strictly less than `count(list)`. */
KTH_EXPORT
void kth_core_data_stack_erase(kth_data_stack_mut_t list, kth_size_t n);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_DATA_STACK_H_ */
