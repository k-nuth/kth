// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compile + link check for the generated mining readers on the chain interface
// (kth_chain_fetch_mining_*). These need a running node to execute, so like the
// rest of the chain interface they are not unit-tested at runtime; this file
// fails the build if any public signature drifts. The behavioral coverage lives
// at the C++ level (fetch_mining_info / difficulty_from_bits tests).

#include <kth/capi.h>
#include <kth/capi/chain/chain_mining.h>
#include <kth/capi/chain/chain_sync.h>
#include <kth/capi/chain/chain_async.h>

static void on_mining_info(kth_chain_t chain, void* ctx, kth_error_code_t ec,
                           kth_mining_info_t info) {
    (void)chain; (void)ctx; (void)ec; (void)info;
}

static void on_mining_template(kth_chain_t chain, void* ctx, kth_error_code_t ec,
                               kth_mining_template_t tmpl,
                               kth_transaction_list_mut_t txs) {
    (void)chain; (void)ctx; (void)ec; (void)tmpl; (void)txs;
}

static void mining_readers_sig_check(kth_chain_t chain) {
    // getmininginfo — both flavors.
    kth_mining_info_t info;
    kth_error_code_t rc = kth_chain_sync_mining_info(chain, &info);
    (void)rc; (void)info;
    kth_chain_async_mining_info(chain, NULL, &on_mining_info);

    // getblocktemplatelight — both flavors (header POD + owned tx list).
    kth_mining_template_t tmpl;
    kth_transaction_list_mut_t txs;
    kth_error_code_t rc2 = kth_chain_sync_mining_template(chain, &tmpl, &txs);
    (void)rc2; (void)tmpl; (void)txs;
    kth_chain_async_mining_template(chain, NULL, &on_mining_template);
}

int main(void) {
    // Referenced but never called: forces linkage of every symbol above
    // without needing a live chain handle.
    void (*ref)(kth_chain_t) = &mining_readers_sig_check;
    (void)ref;
    return 0;
}
