// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_NODE_H_
#define KTH_CAPI_NODE_H_

#include <stdint.h>
#include <stdio.h>

#include <kth/capi/config/settings.h>
#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

KTH_EXPORT
kth_node_t kth_node_construct(kth_settings const* settings, kth_bool_t stdout_enabled);

KTH_EXPORT
void kth_node_destruct(kth_node_t node);

#if ! defined(KTH_DB_READONLY)
KTH_EXPORT
int kth_node_initchain(kth_node_t node);
#endif // ! defined(KTH_DB_READONLY)

#if ! defined(KTH_DB_READONLY)
KTH_EXPORT
void kth_node_init_run_and_wait_for_signal(kth_node_t node, void* ctx, kth_start_modules_t mods, kth_run_handler_t handler);

KTH_EXPORT
void kth_node_init_run(kth_node_t node, void* ctx, kth_start_modules_t mods, kth_run_handler_t handler);

KTH_EXPORT
kth_error_code_t kth_node_init_run_sync(kth_node_t node, kth_start_modules_t mods);
#endif // ! defined(KTH_DB_READONLY)

KTH_EXPORT
void kth_node_signal_stop(kth_node_t node);

KTH_EXPORT
int kth_node_close(kth_node_t node);

KTH_EXPORT
int kth_node_stopped(kth_node_t node);

/**
 * The chain of the running node, or NULL if it is not running.
 *
 * The handle points INSIDE the node and is owned by it. It is valid only while
 * the node runs: do not keep it across kth_node_signal_stop(), kth_node_close()
 * or kth_node_destruct(), which end the node it points into. Ask again after a
 * start rather than holding one from before a stop.
 */
KTH_EXPORT
kth_chain_t kth_node_get_chain(kth_node_t node);

/**
 * The P2P layer of the running node, or NULL if it is not running.
 *
 * Same lifetime as kth_node_get_chain(): owned by the node, valid only while it
 * runs, and not to be kept across a stop.
 */
KTH_EXPORT
kth_p2p_t kth_node_get_p2p(kth_node_t node);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // KTH_CAPI_NODE_H_
