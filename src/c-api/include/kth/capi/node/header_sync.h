// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_NODE_HEADER_SYNC_H_
#define KTH_CAPI_NODE_HEADER_SYNC_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>
#include <kth/capi/callbacks.h>

#if defined(KTH_BROWSER_NODE)

typedef void* kth_header_sync_mut_t;
typedef void const* kth_header_sync_const_t;

/**
 * The caller owns each opaque handle and serializes access on its executor.
 * Keep the handle and callback context alive throughout each scheduled operation.
 * Every terminal callback must have returned before reset or destruction.
 * Stop requests acknowledge admission, not completion of another pending operation.
 * Runtime idleness is diagnostic, not an object lifetime guard.
 */

typedef void (*kth_node_header_sync_async_request_stop_handler_t)(kth_header_sync_const_t self, void* ctx, kth_error_code_t status, uint64_t result);

typedef void (*kth_node_header_sync_async_run_handler_t)(kth_header_sync_mut_t self, void* ctx, kth_error_code_t status, uint64_t result);

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/**
 * bridge_url must be nonempty; capacity must be in 1..2000000.
 * Construction returns a status and writes an owned handle to out on success.
 * On failure out is null; allocation failure is kth_ec_out_of_memory, other exceptions kth_ec_unhandled_exception.
 * @param[out] out Must point to a null `kth_header_sync_mut_t` slot. On success, populated with an owned handle that the caller must release via `kth_node_header_sync_destruct`. Untouched on error.
 */
KTH_EXPORT
kth_error_code_t kth_node_header_sync_construct(char const* bridge_url, kth_network_t network, kth_size_t capacity, KTH_OUT_OWNED kth_header_sync_mut_t* out);


// Destructor

/** No-op if `self` is null. */
KTH_EXPORT
void kth_node_header_sync_destruct(kth_header_sync_mut_t self);


// Getters

/**
 * Keep self and ctx alive until the terminal callback returns; handler must be non-null and must not throw.
 * Inputs are converted and copied before scheduling; terminal completion is delivered exactly once.
 * Allocation failure reports kth_ec_out_of_memory; other C++ exceptions report kth_ec_unhandled_exception.
 */
KTH_EXPORT
void kth_node_header_sync_async_request_stop(kth_header_sync_const_t self, void* ctx, kth_node_header_sync_async_request_stop_handler_t handler);

KTH_EXPORT
uint64_t kth_node_header_sync_last_height(kth_header_sync_const_t self);

KTH_EXPORT
kth_bool_t kth_node_header_sync_running(kth_header_sync_const_t self);

KTH_EXPORT
kth_bool_t kth_node_header_sync_idle(kth_header_sync_const_t self);

/** @return Owned C string. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_status(kth_header_sync_const_t self);

KTH_EXPORT
kth_size_t kth_node_header_sync_index_memory_bytes(kth_header_sync_const_t self);


// Operations

/**
 * Keep self and ctx alive until the terminal callback returns; handler must be non-null and must not throw.
 * Inputs are converted and copied before scheduling; terminal completion is delivered exactly once.
 * Allocation failure reports kth_ec_out_of_memory; other C++ exceptions report kth_ec_unhandled_exception.
 */
KTH_EXPORT
void kth_node_header_sync_async_run(kth_header_sync_mut_t self, void* ctx, kth_node_header_sync_async_run_handler_t handler);

KTH_EXPORT
void kth_node_header_sync_reset(kth_header_sync_mut_t self);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_BROWSER_NODE

#endif // KTH_CAPI_NODE_HEADER_SYNC_H_
