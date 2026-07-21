// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compile + link check for the generated mempool query readers on the chain
// interface (kth_chain_get_mempool_*). These need a running node to execute,
// so like the rest of the chain interface they are not unit-tested at runtime;
// this file fails the build if any public signature drifts. The behavioral
// coverage lives at the C++ level (mempool graph-query tests).

#include <kth/capi.h>
#include <kth/capi/chain/chain_mempool.h>

static void mempool_readers_sig_check(kth_chain_t chain, kth_hash_t const* txid) {
    kth_hash_list_mut_t txids = kth_chain_get_mempool_txids(chain);
    kth_mempool_totals_t info = kth_chain_get_mempool_info(chain);

    kth_mempool_entry_info_t entry;
    kth_bool_t found = kth_chain_get_mempool_entry(chain, txid, &entry);

    kth_hash_list_mut_t depends = kth_chain_get_mempool_depends(chain, txid);
    kth_hash_list_mut_t spentby = kth_chain_get_mempool_spentby(chain, txid);
    kth_hash_list_mut_t ancestors = kth_chain_get_mempool_ancestors(chain, txid);
    kth_hash_list_mut_t descendants = kth_chain_get_mempool_descendants(chain, txid);

    (void)txids; (void)info; (void)found; (void)entry;
    (void)depends; (void)spentby; (void)ancestors; (void)descendants;
}

int main(void) {
    // Referenced but never called: forces linkage of every symbol above
    // without needing a live chain handle.
    void (*ref)(kth_chain_t, kth_hash_t const*) = &mempool_readers_sig_check;
    (void)ref;
    return 0;
}
