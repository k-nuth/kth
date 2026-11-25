// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_NODE_WALLET_H_
#define KTH_CAPI_NODE_WALLET_H_

#if ! defined(KTH_WITH_WALLET)
#error "Wallet support is an optional feature: build with the Conan options neutrino=True and wallet=True."
#endif

#include <stdint.h>

#include <kth/capi/config/network.h>
#include <kth/capi/handles.h>
#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#if ! defined(__EMSCRIPTEN__)

// Wallet support in the node, answered from the node's own chain: its stored
// blocks, its chain state and its mempool, with no index.
//
// Requests and answers are the Electrum protocol's JSON-RPC (Fulcrum's method
// names, arguments and result shapes), answered in this process: no listener
// is opened, and every binding reads the same shapes.

typedef void* kth_wallet_mut_t;
typedef void const* kth_wallet_const_t;

typedef void (*kth_node_wallet_run_handler_t)(kth_wallet_mut_t self, void* ctx, kth_error_code_t status);

/** `answer` is owned by the handler: release it with `kth_core_destruct_string`. */
typedef void (*kth_node_wallet_answer_handler_t)(kth_wallet_mut_t self, void* ctx, kth_error_code_t status, char* answer);

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Answers from a running node's own chain (`chain` from `kth_node_get_chain`,
 * which must outlive the handle).
 * @param[out] out Set to null first; on success an owned handle to release with `kth_node_wallet_destruct`.
 */
KTH_EXPORT
kth_error_code_t kth_node_wallet_construct_from_chain(kth_chain_t chain, kth_network_t network, uint32_t birthday,
    KTH_OUT_OWNED kth_wallet_mut_t* out);

/** Stops it and releases it once every handler given it has returned (so not from inside one). Null is ignored. */
KTH_EXPORT
void kth_node_wallet_destruct(kth_wallet_mut_t self);

/** Keeps the answers current until `kth_node_wallet_stop`; the handler is called once, when it ends. */
KTH_EXPORT
void kth_node_wallet_async_run(kth_wallet_mut_t self, void* ctx, kth_node_wallet_run_handler_t handler);

KTH_EXPORT
void kth_node_wallet_stop(kth_wallet_mut_t self);

/** Where it stands, as JSON. @return Owned C string, or null if it cannot be allocated. Release with `kth_core_destruct_string`. */
KTH_EXPORT KTH_OWNED
char* kth_node_wallet_status(kth_wallet_const_t self);

/**
 * Answers one JSON-RPC request (or a batch) of the Electrum protocol. The
 * handler is called exactly once, with the answer as an owned string.
 */
KTH_EXPORT
void kth_node_wallet_async_request(kth_wallet_mut_t self, char const* request, void* ctx,
    kth_node_wallet_answer_handler_t handler);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ! __EMSCRIPTEN__

#endif // KTH_CAPI_NODE_WALLET_H_
