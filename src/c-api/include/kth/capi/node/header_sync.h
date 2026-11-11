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

typedef void (*kth_node_header_sync_async_request_refresh_handler_t)(kth_header_sync_const_t self, void* ctx, kth_error_code_t status, uint64_t result);

typedef void (*kth_node_header_sync_async_run_handler_t)(kth_header_sync_mut_t self, void* ctx, kth_error_code_t status, uint64_t result);

typedef void (*kth_node_header_sync_async_follow_handler_t)(kth_header_sync_mut_t self, void* ctx, kth_error_code_t status, uint64_t result);

#ifdef __cplusplus
extern "C" {
#endif

// Constructors

/**
 * bridge_url must be nonempty; capacity must be in 1..2000000.
 * Construction returns a status and writes an owned handle to out on success.
 * Allocation failure is kth_ec_out_of_memory, other exceptions kth_ec_unhandled_exception.
 * @param[out] out Must point to a `kth_header_sync_mut_t` slot. Set to null first; on success,
 * populated with an owned handle that the caller must release via `kth_node_header_sync_destruct`.
 */
KTH_EXPORT
kth_error_code_t kth_node_header_sync_construct(char const* bridge_url, kth_network_t network, kth_size_t capacity, KTH_OUT_OWNED kth_header_sync_mut_t* out);


// Destructor

/**
 * Releases the handle. kth_ec_success (also when `self` is null), or kth_ec_operation_failed,
 * releasing nothing, while any operation started on it has not called its handler yet (a run or
 * follow, or a stop or refresh request, even one not yet started): request a stop, wait for the
 * handlers, then destruct. It may be called from the last handler.
 */
KTH_EXPORT
kth_error_code_t kth_node_header_sync_destruct(kth_header_sync_mut_t self);


// Getters

/**
 * Keep self and ctx alive until the terminal callback returns; handler must be non-null and must not throw.
 * Inputs are converted and copied before scheduling; terminal completion is delivered exactly once.
 * Allocation failure reports kth_ec_out_of_memory; other C++ exceptions report kth_ec_unhandled_exception.
 */
KTH_EXPORT
void kth_node_header_sync_async_request_stop(kth_header_sync_const_t self, void* ctx, kth_node_header_sync_async_request_stop_handler_t handler);

/**
 * Keep self and ctx alive until the terminal callback returns; handler must be non-null and must not throw.
 * Inputs are converted and copied before scheduling; terminal completion is delivered exactly once.
 * Allocation failure reports kth_ec_out_of_memory; other C++ exceptions report kth_ec_unhandled_exception.
 */
KTH_EXPORT
void kth_node_header_sync_async_request_refresh(kth_header_sync_const_t self, void* ctx, kth_node_header_sync_async_request_refresh_handler_t handler);

KTH_EXPORT
uint64_t kth_node_header_sync_last_height(kth_header_sync_const_t self);

KTH_EXPORT
kth_bool_t kth_node_header_sync_running(kth_header_sync_const_t self);

KTH_EXPORT
kth_bool_t kth_node_header_sync_idle(kth_header_sync_const_t self);

/** @return Owned C string, or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
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

/**
 * Keep self and ctx alive until the terminal callback returns; handler must be non-null and must not throw.
 * Inputs are converted and copied before scheduling; terminal completion is delivered exactly once.
 * Allocation failure reports kth_ec_out_of_memory; other C++ exceptions report kth_ec_unhandled_exception.
 */
KTH_EXPORT
void kth_node_header_sync_async_follow(kth_header_sync_mut_t self, void* ctx, kth_node_header_sync_async_follow_handler_t handler);

/** Allows a new run after a stop. kth_ec_operation_failed while any operation started on the handle has not called its handler yet. */
KTH_EXPORT
kth_error_code_t kth_node_header_sync_reset(kth_header_sync_mut_t self);

/** @return Owned C string (empty outside the validated chain), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_header_hex(kth_header_sync_const_t self, uint64_t height);

/** @return Owned C string (empty outside the validated chain), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_header_hash(kth_header_sync_const_t self, uint64_t height);

/** The consensus state of the active tip as JSON ("null" at genesis). @return Owned C string, or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_chain_state_snapshot(kth_header_sync_const_t self);

/** The client's effective settings as JSON. @return Owned C string, or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_runtime_settings(kth_header_sync_const_t self);

// Content sampling
// ---------------------------------------------------------------------------

/** Block sampling and transaction observation, while no run is pending. False otherwise. Off by default. */
KTH_EXPORT
kth_bool_t kth_node_header_sync_configure_content(kth_header_sync_mut_t self, kth_bool_t blocks, kth_bool_t transactions);

/** A sampled block as JSON ("null" if not sampled or no longer active), with its coinbase, proof and txids when asked. @return Owned C string, or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_block_metadata(kth_header_sync_const_t self, uint64_t height, kth_bool_t include_transactions);

/** The sampling state as JSON. @return Owned C string, or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_content_state(kth_header_sync_const_t self);

/** The observed transactions as a JSON array. @return Owned C string, or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_observed_transactions(kth_header_sync_const_t self);

// Peers and discovery
// ---------------------------------------------------------------------------

/** Adds a peer while no run is pending: a distinct ws:// or wss:// URL, up to eight in all. False otherwise. */
KTH_EXPORT
kth_bool_t kth_node_header_sync_add_peer(kth_header_sync_mut_t self, char const* url);

/**
 * Endpoint discovery, while no run is pending: whether to negotiate it, accept private endpoints
 * (a LAN experiment) and only WSS ones. False otherwise. Enabled by default.
 */
KTH_EXPORT
kth_bool_t kth_node_header_sync_configure_discovery(kth_header_sync_mut_t self, kth_bool_t enabled, kth_bool_t allow_private, kth_bool_t wss_only);

/** Temporary compatibility with peers using /wsdisc:1/ in user agents. Disabled by default; settled-only. */
KTH_EXPORT
kth_bool_t kth_node_header_sync_configure_legacy_discovery(kth_header_sync_mut_t self, kth_bool_t enabled);

KTH_EXPORT
kth_size_t kth_node_header_sync_peer_count(kth_header_sync_const_t self);

/** The peer's URL. @return Owned C string (empty past the last peer), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_peer_endpoint(kth_header_sync_const_t self, kth_size_t at);

/** "configured", or the endpoint of the peer that advertised it. @return Owned C string (empty past the last peer), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_peer_source(kth_header_sync_const_t self, kth_size_t at);

/** idle, connecting, synchronizing, caught_up, retry, disconnected, rejected or stopped. @return Owned C string (empty past the last peer), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_peer_state(kth_header_sync_const_t self, kth_size_t at);

/** The user agent of its current session; empty when not connected. @return Owned C string (empty past the last peer), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_peer_agent(kth_header_sync_const_t self, kth_size_t at);

/** Its last error. @return Owned C string (empty past the last peer), or null if it cannot be allocated. Caller must release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_header_sync_peer_error(kth_header_sync_const_t self, kth_size_t at);

/** The height of the last header it served on the validated chain; zero past the last peer. */
KTH_EXPORT
uint64_t kth_node_header_sync_peer_height(kth_header_sync_const_t self, kth_size_t at);

KTH_EXPORT
uint64_t kth_node_header_sync_peer_announcements(kth_header_sync_const_t self, kth_size_t at);

KTH_EXPORT
kth_bool_t kth_node_header_sync_peer_connected(kth_header_sync_const_t self, kth_size_t at);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_BROWSER_NODE

#endif // KTH_CAPI_NODE_HEADER_SYNC_H_
