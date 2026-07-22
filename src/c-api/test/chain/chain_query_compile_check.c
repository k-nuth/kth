// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compile + link check for the generated chain query readers
// (kth_chain_sync/async_block_height). They need a running node to execute, so
// like the rest of the chain interface they are not unit-tested at runtime; this
// file fails the build if any public signature drifts.

#include <kth/capi.h>
#include <kth/capi/chain/chain_query.h>
#include <kth/capi/chain/chain_async.h>

static void on_block_height(kth_chain_t chain, void* ctx, kth_error_code_t ec,
                            kth_size_t height) {
    (void)chain; (void)ctx; (void)ec; (void)height;
}

static void query_readers_sig_check(kth_chain_t chain, kth_hash_t const* hash) {
    // Synchronous (blocking) flavor.
    kth_size_t height = 0;
    kth_error_code_t rc = kth_chain_sync_block_height(chain, hash, &height);
    (void)rc; (void)height;

    // Asynchronous (callback) flavor.
    kth_chain_async_block_height(chain, NULL, hash, &on_block_height);
}

int main(void) {
    // Referenced but never called: forces linkage of every symbol above
    // without needing a live chain handle.
    void (*ref)(kth_chain_t, kth_hash_t const*) = &query_readers_sig_check;
    (void)ref;
    return 0;
}
