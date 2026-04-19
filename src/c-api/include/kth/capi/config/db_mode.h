// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CONFIG_DB_MODE_H_
#define KTH_CAPI_CONFIG_DB_MODE_H_

#ifdef __cplusplus
extern "C" {
#endif

// The C entry names (`pruned`, `normal`, `full_indexed`) diverge
// from the C++ `kth::database::db_mode_type` enumerators (`pruned`,
// `blocks`, `full`). Converters in `config/database_helpers.hpp`
// rely on the integer values matching positionally; keep this file
// and the C++ enum in lock-step when either changes.
typedef enum {
    kth_db_mode_pruned = 0,
    kth_db_mode_normal = 1,
    kth_db_mode_full_indexed = 2,
} kth_db_mode_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CONFIG_DB_MODE_H_ */
