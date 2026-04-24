// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_WALLET_COIN_SELECTION_STRATEGY_H_
#define KTH_CAPI_WALLET_COIN_SELECTION_STRATEGY_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // Only selects "clean" UTXOs:
    //   - For BCH needs: selects UTXOs that carry no tokens.
    //   - For token needs: selects UTXOs that carry only the target token.
    //     All UTXOs carry some BCH (the protocol requires it), but token
    //     UTXOs typically carry only the dust minimum (800 sats). Wallets
    //     treat a token output with dust BCH as "BCH = 0" for display
    //     purposes. An output is considered "clean" when it is either pure
    //     BCH (no tokens) or token + dust (no excess BCH).
    // This is the wallet-friendly default. Mixed outputs (tokens + excess
    // BCH, or multiple token categories in one output) confuse wallet
    // balance displays and make spending harder.
    kth_coin_selection_strategy_clean,

    // Allows selecting any UTXO regardless of what it carries.
    // When a selected UTXO carries unrelated tokens, those tokens are reported
    // in collateral_fts (fungible tokens) / collateral_nfts (non-fungible tokens)
    // so the caller can create proper change outputs to preserve them.
    //
    // NOTE: This mode is experimental. Most BCH wallets do not handle mixed
    // outputs correctly. Use only if you understand the implications and your
    // application properly handles collateral token change.
    // Discussion with wallet developers and BCH protocol (CHIP) developers
    // did not reach a conclusive answer on whether mixed mode is ever
    // preferable to clean mode for general wallet use cases.
    // The clean mode exists specifically because
    // mixed outputs created by earlier implementations caused tokens to become
    // "stuck" in outputs that wallets couldn't spend properly. The split
    // template (create_token_split_tx_template) was created to recover
    // tokens from such dirty outputs.
    kth_coin_selection_strategy_mixed,
} kth_coin_selection_strategy_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_WALLET_COIN_SELECTION_STRATEGY_H_ */
