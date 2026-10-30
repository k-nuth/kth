// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/node.h>
#include "node_private.hpp"

#include <kth/capi/helpers.hpp>

#include <kth/node/executor/executor.hpp>

extern "C" {

#if ! defined(KTH_DB_READONLY)
// The barrier reports an exception thrown while starting, but it cannot undo
// what the start already did to the node (threads, databases); the caller still
// owns the node and releases it as usual.
kth_error_code_t kth_node_init_run_sync(kth_node_t node, kth_start_modules_t mods) {
    KTH_PRECONDITION(node != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto ec = kth::cpp_ref<kth::node::executor>(node).start();
        return kth::to_c_err(ec);
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}
#endif // ! defined(KTH_DB_READONLY)

} // extern "C"
