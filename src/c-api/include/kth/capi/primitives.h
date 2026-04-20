// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_PRIMITIVES_H_
#define KTH_CAPI_PRIMITIVES_H_

// Aggregator header. The actual content lives in three siblings split by
// concern:
//
//   - `handles.h`      — scalar typedefs (`kth_size_t`, `kth_bool_t`, ...),
//                        opaque void* handles, hash structs, allocation
//                        contract macros (`KTH_OWNED`, `KTH_PRECONDITION`,
//                        ...) and the destruct entry points.
//   - `callbacks.h`    — every `kth_*_handler_t` callback typedef and the
//                        `kth_debug_step_predicate_t` VM predicate.
//   - the enum headers — `chain/endorsement_type.h`, `chain/point_kind.h`,
//                        `config/*`, `error.h`, `node/start_modules.h`.
//
// Including `primitives.h` continues to expose the full surface so existing
// translation units stay unchanged.

#include <kth/capi/handles.h>
#include <kth/capi/callbacks.h>

#include <kth/capi/chain/endorsement_type.h>
#include <kth/capi/chain/point_kind.h>
#include <kth/capi/config/currency.h>
#include <kth/capi/config/db_mode.h>
#include <kth/capi/config/network.h>
#include <kth/capi/error.h>
#include <kth/capi/node/start_modules.h>
#include <kth/capi/visibility.h>

#endif /* KTH_CAPI_PRIMITIVES_H_ */
