// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Smoke test for the public C surface used by the README's "Using the
// C API" snippet (`kth_settings` defaults → `kth_node_construct` →
// `kth_chain_async_last_height` → `kth_node_init_run_and_wait_for_signal`).
//
// We can't run the snippet verbatim — the README example uses
// `kth_network_mainnet` (400 GiB LMDB cap, real network) and only ever
// returns when the user sends SIGINT. Both are wrong for CI:
//
//   * `kth_network_regtest` instead of `mainnet` — same cap defaults
//     in this tree, but we shrink `db_max_size` to 16 MiB explicitly.
//   * `settings.database.directory` is a fresh per-run temp parent +
//     a `chain` subdir; `kth_node_initchain` requires that it create
//     the leaf itself, so we hand it a path that doesn't exist yet.
//   * `settings.network.threads = 0` keeps the test offline.
//
// Driving the run loop without an external SIGINT is the awkward bit.
// `kth_chain_async_last_height` issued before `init_run_*` core-dumps
// (the chain's dispatcher isn't attached to a threadpool yet) and the
// public C-API has no "node started" hook, so we fall back to a
// worker thread that delays briefly to let the dispatcher come up,
// issues the async query, and exits. The result handler calls
// `kth_node_signal_stop`, which unblocks the main thread's
// `init_run_and_wait_for_signal` so the test can finish without an
// external signal. ctest's TIMEOUT property bounds the worst case.

#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <kth/capi.h>

// `on_height` runs on the chain's internal dispatcher thread, while
// the assertions at the bottom of `main` read the result on the
// initial thread. `pthread_join` only synchronises with the querier
// thread, not the dispatcher, so we hand-roll the cross-thread
// publish/load with C11 atomics. Sequential consistency (the default
// memory order) is fine — the test isn't hot enough to care about
// relaxed orderings.
static atomic_int        g_fetched = 0;
static _Atomic kth_size_t g_height  = (kth_size_t)-1;

// Fires once the chain has resolved the request. Stores the result
// and unblocks the run loop so main can return.
static void on_height(kth_chain_t chain, void* ctx, kth_error_code_t ec, kth_size_t height) {
    (void)chain;
    if (ec == kth_ec_success) {
        atomic_store(&g_height, height);
        atomic_store(&g_fetched, 1);
        printf("Current height: %" PRIu64 "\n", (uint64_t)height);
    }
    kth_node_signal_stop((kth_node_t)ctx);
}

// Sleeps `ms` milliseconds without pulling in platform helpers.
static void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1) {} // resume on EINTR
}

// Worker thread: waits for the chain's dispatcher to come up under
// `init_run_and_wait_for_signal`, then issues the async height query.
// 3 seconds is enough on every CI runner we have today (Linux GHC
// container ~0.05 s, macOS apple-clang ~0.05 s, coverage `-O0` build
// ~0.5 s); ctest's 60 s test-level TIMEOUT bounds the worst case if
// a future runner is somehow much slower.
static void* querier(void* arg) {
    kth_node_t node = (kth_node_t)arg;
    sleep_ms(3000);
    if (kth_node_stopped(node)) return NULL; // shutdown already in flight
    kth_chain_t chain = kth_node_get_chain(node);
    kth_chain_async_last_height(chain, /*ctx=*/node, on_height);
    return NULL;
}

int main(void) {
    // `kth_node_initchain` requires that it create the leaf directory
    // itself — passing an already-existing path makes the underlying
    // `executor::init_directory` fail with `directory_exists`. So we
    // claim a unique parent via `mkdtemp` and hand the unbuilt
    // `chain` subdir inside it to the node.
    char parent[] = "/tmp/kth_readme_c_runtime_XXXXXX";
    if (mkdtemp(parent) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    char chain_dir[512];
    snprintf(chain_dir, sizeof chain_dir, "%s/chain", parent);

    // Same shape as the README example, but with the CI-safe overrides
    // described in the file header.
    kth_settings settings = kth_config_settings_default(kth_network_regtest);
    // `kth_config_settings_default` heap-allocates `database.directory`
    // via `path_to_c` (malloc'd), and the matching cleanup is `free()`
    // in `database_settings_delete`. Pointing the field at our local
    // `chain_dir` array would leak the original allocation and leave
    // a stack pointer where a heap pointer is expected. Free the
    // default and replace it with our own heap copy so the invariant
    // holds; we free our copy ourselves before the test exits.
    free(settings.database.directory);
    settings.database.directory = strdup(chain_dir);
    if (settings.database.directory == NULL) {
        perror("strdup");
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", parent);
        (void)system(cmd);
        return 1;
    }
    settings.database.db_max_size = (uint64_t)16ULL << 20; // 16 MiB
    settings.network.threads = 0;

    kth_node_t node = kth_node_construct(&settings, /*stdout_enabled=*/0);
    if (node == NULL) {
        fprintf(stderr, "kth_node_construct returned NULL\n");
        free(settings.database.directory);
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", parent);
        (void)system(cmd);
        return 1;
    }

    // Initialise the chain database so init_run has something to open.
    if ( ! kth_node_initchain(node)) {
        kth_node_destruct(node);
        free(settings.database.directory);
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", parent);
        (void)system(cmd);
        return 1;
    }

    pthread_t tid;
    if (pthread_create(&tid, NULL, querier, node) != 0) {
        kth_node_destruct(node);
        free(settings.database.directory);
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", parent);
        (void)system(cmd);
        return 1;
    }

    kth_node_init_run_and_wait_for_signal(
        node, /*ctx=*/NULL, kth_start_modules_just_chain, /*handler=*/NULL);

    pthread_join(tid, NULL);
    kth_node_destruct(node);

    // Release our heap copy of the chain directory so the test exits
    // ASAN-clean. (Other inner allocations of `kth_settings` —
    // `node_settings`, `network_settings`, `blockchain_settings` —
    // also leak here; `kth_config_settings_destruct` would clean them
    // up but it also `delete`s the settings pointer itself, which is
    // wrong for our stack-allocated `kth_settings`. Restoring the
    // missing per-field cleanup helpers to the public C-API is a
    // separate fix.)
    free(settings.database.directory);

    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", parent);
    (void)system(cmd);

    if ( ! atomic_load(&g_fetched)) {
        fprintf(stderr, "fetch_last_height did not succeed\n");
        return 1;
    }
    // Freshly initialised regtest = genesis only; height must be 0.
    kth_size_t const height = atomic_load(&g_height);
    if (height != 0) {
        fprintf(stderr, "unexpected height %" PRIu64 "\n", (uint64_t)height);
        return 1;
    }
    return 0;
}
