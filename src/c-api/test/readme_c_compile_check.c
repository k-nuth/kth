// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Verbatim copy of the C snippet in `README.md`'s "Using the C API"
// section. Compiled — but not executed — under `ENABLE_TEST=ON`. Its
// purpose is purely to fail the build if the public surface the README
// example relies on (`kth_settings` layout, `kth_chain_async_last_height`,
// `kth_last_height_fetch_handler_t`, `kth_node_init_run_and_wait_for_signal`,
// `kth_start_modules_all`) drifts.
//
// The runtime smoke for the same flow lives in `readme_c_runtime.c`,
// which exercises the APIs end-to-end on a regtest temp DB.
//
// Keep this file byte-identical to the README's `int main()` body
// minus the includes header.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <kth/capi.h>

// Fires once the chain has resolved the request.
static void on_height(kth_chain_t chain, void* ctx, kth_error_code_t ec, kth_size_t height) {
    (void)chain; (void)ctx;
    if (ec == kth_ec_success) {
        printf("Current height: %" PRIu64 "\n", (uint64_t)height);
    }
}

int main(void) {
    // Mainnet defaults.
    kth_settings settings = kth_config_settings_default(kth_network_mainnet);

    // Override individual settings if you need to, e.g.:
    //   settings.database.directory = "/var/lib/kth";
    //   settings.network.threads    = 8;

    kth_node_t node = kth_node_construct(&settings, /*stdout_enabled=*/1);

    // Submit the height query asynchronously; on_height fires once the chain
    // can answer it.
    kth_chain_t chain = kth_node_get_chain(node);
    kth_chain_async_last_height(chain, /*ctx=*/NULL, on_height);

    // Bring the node up and block until SIGINT/SIGTERM. The async query
    // resolves while the node is running.
    kth_node_init_run_and_wait_for_signal(node, NULL, kth_start_modules_all, NULL);

    kth_node_destruct(node);
    return 0;
}
