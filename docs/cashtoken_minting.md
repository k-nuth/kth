# CashTokens minting API

Wallet-level API for building CashTokens transactions (genesis, mint,
transfer, burn) as **unsigned templates**. Signing is left to the
caller so the same API fits local keys, hardware wallets, and
WalletConnect flows.

- Header: [`kth/domain/wallet/cashtoken_minting.hpp`](../src/domain/include/kth/domain/wallet/cashtoken_minting.hpp)
- Namespace: `kth::domain::wallet::cashtoken`
- Tests: [`src/domain/test/wallet/cashtoken_minting.cpp`](../src/domain/test/wallet/cashtoken_minting.cpp)


## Table of contents

1. [Conventions](#conventions)
2. [Script flags — choose the target ruleset](#script-flags--choose-the-target-ruleset)
3. [Output factories](#output-factories)
4. [`prepare_genesis_utxo`](#prepare_genesis_utxo) — carve an index-0 UTXO
5. [`create_token_genesis`](#create_token_genesis) — create a new category
6. [`create_token_mint`](#create_token_mint) — mint NFTs from a minting NFT
7. [`create_token_transfer`](#create_token_transfer) — send FT or NFT
8. [`create_token_burn`](#create_token_burn) — destroy FT / NFT
9. [`create_ft`](#create_ft) — high-level FT genesis
10. [`create_nft_collection`](#create_nft_collection) — plan a multi-TX collection
11. [End-to-end walkthroughs](#end-to-end-walkthroughs)
12. [Error codes](#error-codes)


## Conventions

- **Unsigned transactions.** Every builder returns a `chain::transaction`
  whose input `scriptSig`s are empty and a `signing_indices` vector
  listing which inputs the caller must sign (always all of them for
  P2PKH flows but exposed for future signing strategies).
- **`std::expected`.** Every function returns `std::expected<Result,
  std::error_code>`; errors come from `kth::error::*` so they compose
  cleanly with the rest of the domain library.
- **Fee UTXOs must be BCH-only.** Passing a token-carrying UTXO in
  `fee_utxos` is rejected with `token_invalid_category`: silently
  consuming those tokens as fees would be a catastrophic accounting bug.
  The same rule applies to the **primary BCH input** of
  `prepare_genesis_utxo` and `create_token_genesis` — a token-bearing
  UTXO passed there would have its payload silently burned.
- **Coin selection is explicit.** The caller selects which `token_utxos`
  and `fee_utxos` to spend; the builder doesn't reach into a wallet.
- **Address validity is checked at every level.** Not only the primary
  `destination` / `creator_address`, but also every resolved
  `change_address`, `minting_destination`, `token_change_address`, and
  `bch_change_address` is rejected if default-constructed — an optional
  wrapping a default-constructed address would otherwise slip past
  `has_value()` and produce unspendable change outputs, permanently
  burning the tokens or BCH sent there.
- **NFT capability is validated.** `chain::capability_t` can hold any
  `uint8_t` via casts; each builder that creates or references a
  commitment verifies the byte is one of `none` / `mut` / `minting`
  before building the output.
- **Fee estimation is token-aware.** Token-bearing outputs are sized
  against their actual serialised length (category + bitfield +
  commitment + optional FT amount on top of the P2PKH script), not the
  naive 34-byte P2PKH approximation. At the post-Leibniz 128-byte
  commitment cap, the difference is ~170 bytes per output — enough to
  push a naive fee below the relay floor.


## Script flags — choose the target ruleset

CashTokens scheduled the NFT-commitment size cap to grow over time:

| Upgrade             | Constant                                    | Cap       | Activates |
|---------------------|---------------------------------------------|-----------|-----------|
| Descartes (current) | `max_token_commitment_length_descartes`     | 40 bytes  | 2023-May  |
| Leibniz (upcoming)  | `max_token_commitment_length_leibniz`       | 128 bytes | 2026-May  |

Params structs that create or modify a commitment expose a required
`script_flags_t script_flags` field (no default — must be set
explicitly):

```cpp
token_genesis_params p{};
p.script_flags = 0;                                    // Descartes  (40 B)
// or
p.script_flags = machine::script_flags::bch_loops;      // Leibniz    (128 B)
```

The impl calls `chain::max_token_commitment_length(flags)` to pick the
right cap. `script_flags` is **not** defaulted in the struct on purpose
— a wrongly-defaulted flag silently produces either an
under-constrained TX (rejected at broadcast) or an over-constrained one
(needlessly rejected commitments).


## Output factories

Pure helpers that build a single `chain::output`. They do **not** build
transactions or perform coin selection — they are the primitive used by
the transaction builders below.

```cpp
chain::output create_ft_output(
    payment_address const& destination,
    hash_digest const& category_id,
    uint64_t ft_amount,
    uint64_t satoshis = 1000);

chain::output create_nft_output(
    payment_address const& destination,
    hash_digest const& category_id,
    chain::capability_t capability,
    data_chunk const& commitment,
    uint64_t satoshis = 1000);

chain::output create_combined_token_output(
    payment_address const& destination,
    hash_digest const& category_id,
    uint64_t ft_amount,
    chain::capability_t capability,
    data_chunk const& commitment,
    uint64_t satoshis = 1000);
```


## `prepare_genesis_utxo`

Produces a TX whose output 0 is a legal genesis input (the CashTokens
CHIP requires the genesis input to spend output #0 of its parent).

```cpp
prepare_genesis_params p{};
p.utxo = wallet_utxo;                 // any spendable UTXO
p.destination = my_address;
p.satoshis = 10'000;                  // value for output 0

auto r = prepare_genesis_utxo(p);
if ( ! r) return r.error();

// sign r->transaction using r->signing_indices, broadcast
// output 0 of the signed TX is the `genesis_utxo` for create_token_genesis
```

> The txid of the returned TX is unknown until signing, so
> `prepare_genesis_utxo` does **not** return an outpoint. After signing,
> the caller computes it as `{signed_tx.hash(), 0}`.


## `create_token_genesis`

Creates a new CashToken category. The `category_id` equals the txid of
the genesis input's parent transaction.

```cpp
token_genesis_params p{};
p.genesis_utxo = index_zero_utxo;     // outpoint.index() MUST be 0
p.destination = my_address;
p.ft_amount = 1'000'000;              // optional: fungible supply
p.nft = nft_spec{      // optional: NFT side
    chain::capability_t::minting,
    data_chunk{0x00}};
p.fee_utxos = { extra_bch_utxo };
p.change_address = my_address;
p.script_flags = 0;                   // Descartes ruleset

auto r = create_token_genesis(p);
auto const category_id = r->category_id;
```

Payload combinations:

| `ft_amount` | `nft`       | Output 0                                      |
|-------------|-------------|-----------------------------------------------|
| set         | not set     | pure fungible token                           |
| not set     | set         | pure NFT (often the minting NFT)              |
| set         | set         | combined (FT supply + NFT in one output)      |
| not set     | not set     | rejected with `operation_failed`              |

`ft_amount` must be in `[1, INT64_MAX]`. Commitments are capped per the
active `script_flags` (see [Script flags](#script-flags--choose-the-target-ruleset)).


## `create_token_mint`

Mints one or more new NFTs from an existing **minting NFT**.

```cpp
token_mint_params p{};
p.minting_utxo = current_minting_utxo;         // must have capability == minting
p.minting_destination = creator_address;       // where the preserved minting NFT goes
p.new_minting_commitment = data_chunk{0x05};   // optional: advance counter
p.nfts = {
    { buyer_a_address, data_chunk{0x01}, chain::capability_t::none, 800 },
    { buyer_b_address, data_chunk{0x02}, chain::capability_t::none, 800 },
};
p.fee_utxos = { extra_bch_utxo };
p.change_address = creator_address;
p.script_flags = 0;

auto r = create_token_mint(p);
// r->minted_output_indices tells you which outputs carry the new NFTs
// (output 0 always holds the preserved minting NFT)
```

Outputs:

- **Output 0** — preserved minting NFT (same category, same capability
  `minting`; commitment replaced if `new_minting_commitment` set).
- **Outputs 1..N** — one per entry of `nfts`, each immutable unless
  the caller asks for `mutable`/`minting` explicitly.
- **Last output (optional)** — BCH change.

The `minting_utxo`'s BCH carrier must itself be ≥ `token_dust_limit`
(800 sats); otherwise output 0 would be below dust and the TX wouldn't
relay.


## `create_token_transfer`

Sends a fungible amount *or* a specific NFT to `destination`. Exactly
one of `ft_amount` / `nft` must be set.

```cpp
// FT transfer
token_transfer_params p{};
p.token_utxos = { ft_utxo };
p.destination = buyer_address;
p.ft_amount = 500;
p.token_change_address = my_address;
p.bch_change_address = my_address;
p.fee_utxos = { extra_bch_utxo };
auto r = create_token_transfer(p);

// NFT transfer
p.token_utxos = { nft_utxo };
p.destination = buyer_address;
p.ft_amount.reset();
p.nft = nft_spec{ chain::capability_t::none, target_commitment };
auto r = create_token_transfer(p);
```

All `token_utxos` must share the same category (mixed categories are
rejected up front to avoid silent burns). Carried NFTs that aren't the
selected one become change outputs at `token_change_address`.


## `create_token_burn`

Destroys fungible tokens, the NFT, or both. Optionally attaches an
OP_RETURN message.

```cpp
token_burn_params p{};
p.token_utxo = existing_token_utxo;
p.burn_ft_amount = 300;               // optional: partial FT burn
p.burn_nft = false;                   // optional: also destroy the NFT
p.message = std::string{"supply closed"};  // optional: OP_RETURN (≤ 220 B)
p.destination = my_address;           // where remaining value / tokens go
p.fee_utxos = { extra_bch_utxo };
auto r = create_token_burn(p);
```

- Partial burns re-emit the remainder (the output is clean: pure FT,
  pure NFT, or combined — whichever survives).
- `message` is capped at the 220-byte OP_RETURN standardness limit; a
  larger message is rejected (its TX would not relay).
- The OP_RETURN output's actual serialised size is used in the fee
  estimate — the P2PKH approximation understates OP_RETURN outputs by
  up to ~200 sats at the 220-byte limit.


## `create_ft`

High-level convenience wrapper over `create_token_genesis` for the two
most common FT flows: pure FT with fixed supply, or FT paired with a
minting NFT.

```cpp
ft_params p{};
p.genesis_utxo = index_zero_utxo;
p.destination = my_address;
p.total_supply = 21'000'000;
p.with_minting_nft = false;           // true to also mint a minting NFT
p.fee_utxos = { extra_bch_utxo };
p.script_flags = 0;
auto r = create_ft(p);
```


## `create_nft_collection`

Plans a multi-TX NFT collection: genesis + N mint batches + optional
final burn. Because each subsequent TX's outpoints depend on txids that
are only known after signing, the collection builder returns a **plan**
(not a chain of unsigned TXs). The caller threads each signed TX's
output 0 into the next `create_token_mint` call.

```cpp
nft_collection_params p{};
p.genesis_utxo = index_zero_utxo;
p.creator_address = my_address;
p.nfts.reserve(1000);
for (int i = 1; i <= 1000; ++i) {
    p.nfts.push_back({ data_chunk{/* commitment bytes */}, /*destination=*/std::nullopt });
}
p.batch_size = 500;                    // NFTs per mint TX; must be > 0
p.keep_minting_token = false;          // false → burn minting NFT at the end
p.fee_utxos = { extra_bch_utxo };
p.script_flags = 0;

auto r = create_nft_collection(p);
// r->genesis_transaction   — sign & broadcast first
// r->category_id           — already known (= genesis_utxo.point().hash())
// r->batches               — feed each batch into create_token_mint
// r->final_burn            — if true, create_token_burn the minting NFT last
```

Exec sketch:

```cpp
// 1. Genesis
auto signed_genesis = sign(r->genesis_transaction);
broadcast(signed_genesis);
chain::point next_minting_outpoint{ signed_genesis.hash(), 0 };

// 2. Mint batches
for (auto const& batch : r->batches) {
    token_mint_params mp{};
    mp.minting_utxo = utxo_from(next_minting_outpoint, ...);
    mp.nfts = batch.mint_requests;
    mp.minting_destination = my_address;
    mp.fee_utxos = fresh_fee_utxos();
    mp.script_flags = 0;
    auto mint = create_token_mint(mp);
    auto signed_mint = sign(mint->transaction);
    broadcast(signed_mint);
    next_minting_outpoint = { signed_mint.hash(), 0 };
}

// 3. Optional final burn
if (r->final_burn) {
    token_burn_params bp{};
    bp.token_utxo = utxo_from(next_minting_outpoint, ...);
    bp.burn_nft = true;
    bp.destination = my_address;
    auto burn = create_token_burn(bp);
    broadcast(sign(burn->transaction));
}
```


## End-to-end walkthroughs

### Create a fungible token (no NFT)

```cpp
// 1. Prepare an index-0 UTXO
prepare_genesis_params prep{};
prep.utxo = any_wallet_utxo;
prep.destination = my_address;
auto prep_r = prepare_genesis_utxo(prep);
auto signed_prep = sign(prep_r->transaction);
broadcast(signed_prep);

// 2. Build genesis
ft_params gen{};
gen.genesis_utxo = utxo_from({signed_prep.hash(), 0}, ...);
gen.destination = my_address;
gen.total_supply = 21'000'000;
gen.fee_utxos = fresh_fee_utxos();
gen.script_flags = 0;
auto gen_r = create_ft(gen);
broadcast(sign(gen_r->transaction));
// category_id = gen_r->category_id
```

### Mint a 1,000-NFT collection

Use `create_nft_collection` (see above) and iterate
through `r->batches`. Each batch TX mints up to `batch_size` NFTs in one
transaction; the caller decides how many NFTs fit in a single TX based
on the desired transaction size and the commitment-size cap.


### Transfer a fungible amount (with token change)

```cpp
// Send 300 GUA from a UTXO holding 1,000 GUA. 700 GUA comes back as
// change. Fee is covered by a separate BCH UTXO.
token_transfer_params p{};
p.token_utxos = { gua_1000_utxo };
p.destination = buyer_address;
p.ft_amount = 300;
p.token_change_address = my_address;
p.bch_change_address = my_address;
p.fee_utxos = { bch_utxo_50k };
auto r = create_token_transfer(p);
// Output 0: 300 GUA → buyer
// Output 1: 700 GUA (token change) → my_address
// Output 2: BCH change → my_address
```


### Transfer a specific NFT by commitment

```cpp
// Find and send the NFT with commitment 0xDEAD (imagine it's one of
// many NFTs in the wallet for the same collection). Any other NFTs
// present in `token_utxos` pass through to `token_change_address`.
data_chunk target_commitment{0xDE, 0xAD};
token_transfer_params p{};
p.token_utxos = { nft_dead_utxo, nft_other_utxo };   // multiple NFTs OK
p.destination = recipient_address;
p.nft = nft_spec{chain::capability_t::none, target_commitment};
p.token_change_address = my_address;
p.bch_change_address = my_address;
p.fee_utxos = { bch_utxo_50k };
auto r = create_token_transfer(p);
// Output 0: NFT 0xDEAD → recipient
// Output 1: NFT (other) → my_address (pass-through)
// Output 2: BCH change
```


### Burn fungible tokens

```cpp
// Destroy 200 GUA from a UTXO holding 1,000 GUA; the remaining 800 is
// re-emitted as a clean FT output at destination.
token_burn_params p{};
p.token_utxo = gua_1000_utxo;
p.burn_ft_amount = 200;
p.destination = my_address;
p.fee_utxos = { bch_utxo_50k };
auto r = create_token_burn(p);
// Output 0: 800 GUA → my_address
```


### Close a collection by burning the minting NFT

```cpp
// After the last mint batch, burn the minting NFT to permanently cap
// the supply. Optional OP_RETURN records the rationale on-chain.
token_burn_params p{};
p.token_utxo = final_minting_utxo;
p.burn_nft = true;
p.message = std::string{"supply capped at 10000"};
p.destination = my_address;
p.fee_utxos = { bch_utxo_50k };
auto r = create_token_burn(p);
// Output 0: plain BCH at my_address (minting NFT destroyed)
// Output 1: OP_RETURN "supply capped at 10000"
// Output 2: BCH change
```


### Burn only the NFT portion of a both-kinds UTXO

```cpp
// A both-kinds UTXO carries 500 GUA + a minting NFT. Burn only the NFT
// (supply closure) and keep the FT side as a clean FT output.
token_burn_params p{};
p.token_utxo = both_kinds_utxo;                // 500 GUA + minting NFT
p.burn_nft = true;
p.destination = my_address;
p.fee_utxos = { bch_utxo_50k };
auto r = create_token_burn(p);
// Output 0: 500 GUA (fungible_only) → my_address
```


### Self-mint for a private drop

```cpp
// The creator holds a minting NFT and wants to mint 5 NFTs directly to
// their own wallet (no public sale yet). The minting NFT commitment is
// advanced to act as a counter.
token_mint_params p{};
p.minting_utxo = current_minting_utxo;         // commitment = 0x04 (4 minted so far)
p.minting_destination = creator_address;
p.new_minting_commitment = data_chunk{0x09};   // next index = 9
p.fee_utxos = { bch_utxo_50k };
p.script_flags = 0;                            // Descartes (40-byte cap)
for (uint8_t i = 5; i <= 9; ++i) {
    p.nfts.push_back({
        creator_address,
        data_chunk{i},                         // commitment = NFT index
        chain::capability_t::none,
        800
    });
}
auto r = create_token_mint(p);
// Output 0: minting NFT (commitment=0x09) → creator
// Outputs 1..5: NFTs #5..#9 → creator
```


### Target a post-Leibniz ruleset (128-byte commitments)

```cpp
// Opt into Leibniz rules so the impl accepts commitments up to 128
// bytes instead of the default 40. Only needed when the target network
// has Leibniz active (from 2026-May).
token_genesis_params p{};
p.genesis_utxo = index_zero_utxo;
p.destination = my_address;
p.nft = nft_spec{
    chain::capability_t::minting,
    data_chunk(80, 0x00)                       // 80-byte commitment
};
p.fee_utxos = { bch_utxo_50k };
p.script_flags = machine::script_flags::bch_loops;   // Leibniz gate
auto r = create_token_genesis(p);              // accepted; under Descartes it would be rejected
```


### Combined genesis: FT supply + minting NFT in one output

```cpp
// Create 21M GUA tokens AND a minting NFT with a single genesis. Both
// live in output 0 as a both-kinds payload.
token_genesis_params p{};
p.genesis_utxo = index_zero_utxo;
p.destination = creator_address;
p.ft_amount = 21'000'000;
p.nft = nft_spec{
    chain::capability_t::minting,
    data_chunk{0x00}
};
p.fee_utxos = { bch_utxo_50k };
p.script_flags = 0;
auto r = create_token_genesis(p);
// Output 0: [category_id, 21M GUA + minting NFT] → creator
```


### Prepare a genesis UTXO when none is available

```cpp
// The wallet has UTXOs but none at output index 0. Carve one first.
prepare_genesis_params prep{};
prep.utxo = some_non_index_zero_utxo;          // e.g. (txid, vout=2)
prep.destination = my_address;
auto prep_r = prepare_genesis_utxo(prep);
auto signed_prep = sign(prep_r->transaction);
broadcast(signed_prep);

// The carved UTXO is now available at output 0 of the signed TX.
// Build a utxo object for the next step:
chain::utxo carved{
    chain::output_point{signed_prep.hash(), 0},
    /*amount=*/ 10'000,
    /*token_data=*/ std::nullopt
};

// Use it as the genesis input.
token_genesis_params gen{};
gen.genesis_utxo = carved;
gen.destination = my_address;
gen.ft_amount = 1'000'000;
gen.fee_utxos = { other_bch_utxo };
gen.script_flags = 0;
auto gen_r = create_token_genesis(gen);
broadcast(sign(gen_r->transaction));
```


## Error codes

Errors returned from this API come from `kth::error::*`:

| Error                         | Cause                                                          |
|-------------------------------|----------------------------------------------------------------|
| `insufficient_amount`         | Not enough BCH to cover outputs + fee                          |
| `empty_utxo_list`             | Required `token_utxos` list is empty                           |
| `invalid_change`              | Token change required but no `token_change_address` provided, or a resolved change address is default-constructed |
| `operation_failed`            | Malformed request: missing/bad destination, invalid `capability` byte, missing payload, OP_RETURN > 220 B, etc. |
| `token_invalid_category`      | Missing/mismatched token category, non-minting mint UTXO, token-carrying `fee_utxo`, or a token-carrying primary input of `prepare_genesis_utxo` / `create_token_genesis` |
| `token_commitment_oversized`  | Commitment exceeds the cap for the active `script_flags`       |
| `token_fungible_insufficient` | Requested FT move/burn exceeds the input FT total              |
| `token_amount_overflow`       | Fungible amount is zero (in a `create_token_transfer`) or out of the `[1, INT64_MAX]` range |
