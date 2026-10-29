// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node.h>
#include "node_private.hpp"

#include <kth/capi/helpers.hpp>

#include <kth/node/executor/executor.hpp>

extern "C" {

#if ! defined(KTH_DB_READONLY)
kth_error_code_t kth_node_init_run_sync(kth_node_t node, kth_start_modules_t mods) {
    auto ec = kth_node_cpp(node).start();
    return kth::to_c_err(ec);
}
#endif // ! defined(KTH_DB_READONLY)

} // extern "C"
