# JSON-RPC interface

Knuth exposes an optional JSON-RPC-over-HTTP interface, compatible in shape with
the Bitcoin Cash Node / Bitcoin Core RPC that mining and wallet tooling expects.

The JSON-RPC server and the C-API are two frontends over the same C++ core
(`kth::blockchain::block_chain`). **Every JSON-RPC method has a C-API
counterpart**; the [equivalence table](#method-equivalences) below is the source
of truth for that mapping.

## Building

The server is **off by default** and gated at compile time:

```
conanfile option  rpc=True   →   CMake KTH_WITH_RPC   →   #if defined(KTH_WITH_RPC)
```

The transport uses [llhttp](https://github.com/nodejs/llhttp) (the Node.js HTTP
parser, sans-io) fed by the node's existing standalone-asio coroutine loop, plus
[simdjson](https://github.com/simdjson/simdjson) for both request parsing and
response serialization. `llhttp` is only pulled in when the `rpc` option is enabled.

## Running

The server is also **off at runtime by default**. Enable it in the config file:

```ini
rpc.enabled  = true
rpc.bind     = 127.0.0.1   # localhost only by default
rpc.port     = 8332
rpc.user     = myuser      # HTTP Basic-Auth; empty => .cookie file
rpc.password = mypass
```

Starting a build without RPC support but with `rpc.enabled = true` logs a warning
and continues without the server.

### Authentication

HTTP Basic-Auth, bitcoind-style:

- If `rpc.user` / `rpc.password` are set, clients authenticate with those.
- Otherwise the server generates a random token and writes `__cookie__:<token>`
  to a `.cookie` file; clients read it (e.g. `curl --user $(cat .cookie)`).

Unauthenticated requests get `401 Unauthorized`. Only `POST` is accepted.

### Example

```bash
curl --user myuser:mypass -H 'content-type: application/json' \
     -d '{"jsonrpc":"2.0","id":1,"method":"getblockcount","params":[]}' \
     http://127.0.0.1:8332/
# => {"result":0,"error":null,"id":1}
```

Responses use the Bitcoin envelope `{"result":..., "error":..., "id":...}`. Error
codes follow Bitcoin Core (`-32700` parse error, `-32601` method not found, ...).

## Method equivalences

Each row maps a JSON-RPC method to its C-API function and the underlying C++
`block_chain` method. New methods must be added here together with their C-API
counterpart.

| JSON-RPC method | C-API function | C++ (`block_chain`) |
|---|---|---|
| `getblockcount` | `kth_chain_sync_last_height` | `fetch_last_height().block` |
| `getbestblockhash` | `kth_chain_sync_block_hash` | `get_last_heights()` + `get_block_hash()` |
| `getblockhash` | `kth_chain_sync_block_hash` | `get_block_hash(height)` |
| `getdifficulty` | `kth_chain_sync_mining_info` | `fetch_mining_info().difficulty` |
| `getblockchaininfo` | `kth_chain_sync_mining_info` + `kth_chain_sync_block_hash` | `chain_view()` + `synchronization(view, header_tip)` + `headers()` |
| `getnetworkhashps` | `kth_chain_sync_block_header_by_height` | `chain_view()` + `headers()` (chain work, timestamps) |
| `getchaintips` | — (the C-API does not expose the header index) | `chain_view()` + `headers()` |
| `getnetworkinfo` | `kth_network_settings` + `kth_blockchain_settings.byte_fee_satoshis` | `p2p_node::network_settings()` + `connection_count()` + `stopped()` |
| `getrawtransaction` | `kth_chain_sync_transaction` | `fetch_transaction(hash)` |
| `getblock` | `kth_chain_sync_block_by_hash` | `fetch_block(hash)` |
| `getblockheader` (`verbose=false`: raw 80-byte header hex; verbose: `confirmations`, -1 off the connected chain) | `kth_chain_sync_block_header_by_hash` | `fetch_block_header(hash)` + `chain_view()` |
| `sendrawtransaction` | `kth_chain_sync_organize_transaction` | `organize(tx)` |
| `validateaddress` | `kth_wallet_payment_address_parse_from` | `wallet::payment_address::parse_from(address, network)` |
| `decoderawtransaction` | `kth_chain_transaction_construct_from_data` | `transaction::from_data()` |
| `gettxout` | — (no C-API UTXO reader yet) | `get_utxo()` + `utxo_resolve()` + `mempool_ref().spender()` / `resolve()` |
| `getrawmempool` | `kth_chain_get_mempool_txids` | `get_mempool_txids()` |

| `getrawmempool` (`verbose=true`: entries keyed by txid) | `kth_chain_get_mempool_txids` (+ `kth_chain_get_mempool_entry`) | `get_mempool_txids()` (+ `get_mempool_entry()`) |
| `getmempoolinfo` | `kth_chain_get_mempool_info` | `get_mempool_info()` |
| `estimatefee` | `kth_blockchain_settings.byte_fee_satoshis` | `chain_settings().byte_fee_satoshis` |
| `getmempoolentry` | `kth_chain_get_mempool_entry` | `get_mempool_entry()` (+ depends/spentby) |
| `getmempoolancestors` | `kth_chain_get_mempool_ancestors` | `get_mempool_ancestors()` |
| `getmempooldescendants` | `kth_chain_get_mempool_descendants` | `get_mempool_descendants()` |
| `getblocktemplate` | `kth_chain_sync_mining_template` / `kth_chain_async_mining_template` | `fetch_mining_template()` |
| `getblocktemplatelight` | `kth_chain_sync_mining_template` / `kth_chain_async_mining_template` | `fetch_mining_template()` |
| `getmininginfo` | `kth_chain_sync_mining_info` / `kth_chain_async_mining_info` | `fetch_mining_info()` |
| `submitblocklight` | `kth_chain_sync_organize_block` / `kth_chain_async_organize_block` | `organize(block)` |
| `help` | — (describes this server) | `dispatcher::methods()` |

Each async chain reader is exposed in both a blocking (`kth_chain_sync_*`) and a
callback (`kth_chain_async_*`) flavor.

_More methods land per phase: mining (`submitblock`, `getmininginfo`), mempool
(`getrawmempool`, `getmempoolentry`, ...), and blockchain/raw-tx (`getblock`,
`getrawtransaction`, `sendrawtransaction`, ...)._

## help

`help` returns the methods this server answers, one per line. `help "<method>"`
returns the method's name, or `help: unknown command: <method>` for a name it
does not answer; there is no per-method help text yet. Method names and JSON
help results are prepared during registration. Custom methods must be
registered before the built-ins, which install `help` last. An omitted parameter
or an empty string lists all methods. Parameters must be an array containing
at most one string; other types or extra arguments return invalid params.

## estimatefee

`estimatefee` returns the fee per kB, in BCH, that the size part of the node's
transaction price asks for: `node.byte_fee_satoshis` per byte, rounded to whole
satoshis per kB (`0.000001` with the default `0.1`). The price also charges
`node.sigop_fee_satoshis` per signature operation, which a per-kB rate cannot
carry, so a transaction pays that on top. The node has no fee estimator: the
target-blocks argument older clients send is accepted and does not change the
answer.

## decoderawtransaction

`decoderawtransaction "<hex>"` decodes a serialized transaction without looking
anything up: `txid`, `hash`, `version`, `size`, `locktime`, `vin` (for a coinbase
just `coinbase` and `sequence`) and `vout`. Each output has `value` in BCH, `n`,
`scriptPubKey` (`asm`, `hex`, `type`: `pubkeyhash`, `scripthash`, `pubkey`,
`multisig`, `nulldata` or `nonstandard`, and for a spendable kind `reqSigs` and
`addresses` as cashaddr under the node's network prefix) and, when it carries
CashTokens, `tokenData` (`category`, `amount` as a string, and `nft` with
`capability` and `commitment`). `scriptPubKey.hex` leaves out the token prefix.
`asm` is kth's own script notation, under the node's current script rules.

## getnetworkhashps

`getnetworkhashps [nblocks=120] [height=-1]` returns the hashes per second the
network did over the `nblocks` blocks ending at `height` on the connected chain:
the chain work those blocks add, divided by the time between the earliest and
the latest timestamp among them and the block before them. `nblocks` must be a positive 32-bit integer and the window stops at the genesis
block; a
`height` of -1, or past the connected tip, means the connected tip. It is `0`
when those timestamps span no time, or when the selected block is genesis.
Parameters must be an array with at most two integers (or null for defaults);
values outside the signed 32-bit range are rejected.

## getblockchaininfo

Returns `chain` (`main`, `test`, `test4`, `scale`, `chip` or `regtest`), `blocks`
(connected tip height), `headers`, `bestblockhash`,
`difficulty` (of the next required work), `mediantime` (median time past of the
connected tip), `verificationprogress`, `initialblockdownload`, `chainwork`,
`size_on_disk` and `warnings`. The tip fields come from one published chain
view, so they always describe the same tip.

- `verificationprogress` is the connected tip's share of the header chain by
  height, `1.0` once they meet.
- `initialblockdownload` is `true` until the node is synchronized: its connected
  tip has reached the header tip, and that tip is within
  `chain.notify_limit_hours` of now. These are the two conditions
  `getmininginfo` reports as `caughtup` and `fresh`.
- `chainwork` is the connected tip's cumulative work, as 64 hex digits. It is
  left out while the header index cannot name the tip.
- `size_on_disk` is the size of the block and undo files as they are on disk,
  including the space each file has preallocated for the blocks to come (block
  files grow in 128 MiB chunks). Only `blk`/`rev` files with numbered `.dat`
  names count, symlinked ones included. A file that cannot be read is skipped;
  it never turns the whole call into an error.
- `pruned` is not reported.

## getchaintips

`getchaintips` returns every chain tip the header index knows, highest first,
each with `height`, `hash`, `branchlen` (blocks from the connected chain to the
tip) and `status`: `active` for the connected tip, `valid-headers` for a branch
whose blocks the node all holds, and `headers-only` for one it does not. The
header index does not record whether a block failed validation, so `invalid`
and `valid-fork` are not reported.

## gettxout

`gettxout "<txid>" <n> [include_mempool=true]` returns the output if it is
unspent and `null` otherwise: `bestblock` (the connected tip it was read
against), `confirmations`, `value` in BCH, `scriptPubKey` and `tokenData` as
`decoderawtransaction` shows them, and `coinbase`. With `include_mempool`, an
output that a mempool transaction spends is `null`, and an output of a mempool
transaction is returned with `0` confirmations.
## Mining: getblocktemplate

`getblocktemplate [template_request]` returns the next-block template with its
transactions in full, as BIP22 describes, for pools that build the coinbase and
submit the whole block: `capabilities`, `version`, `previousblockhash`,
`transactions` (each with `data`, `txid`, `hash`, `depends` when it spends an
earlier template transaction, and `fee` and `sigops`, the transaction's
sigchecks, from the captured selection), `coinbaseaux`, `coinbasevalue`,
`target`, `mintime`, `mutable`, `noncerange`, `sigoplimit` (the
block's sigchecks limit), `sizelimit`, `curtime`, `bits` and `height`. It is the
same template `getblocktemplatelight` serves, so the two agree on the selection.
Only `template` mode is supported; long polling is unsupported, so `longpollid`
is omitted. Fees and sigchecks are captured with the selected transactions and
remain unchanged when a cached template is served after mempool changes.

## getnetworkinfo

`getnetworkinfo` returns `version`, `subversion` (the user agent the node sends
its peers), `protocolversion`, `localservices` (16 hex digits) with
`localservicesnames`, `localrelay`, `networkactive`, `connections`, `relayfee`
and `warnings`. `version` is `kth::version` as one number:
`major * 1000000 + minor * 10000 + patch * 100`, so 1.5.2 is 1050200.
`networkactive` is false while the P2P node is not running. `relayfee` is the
size part of the node's transaction price, `node.byte_fee_satoshis` per byte, in
BCH per kB; the price also charges `node.sigop_fee_satoshis` per signature
operation. The method takes no arguments: anything other than omitted params or
an empty array is a `-32602` error.

## Mining: getblocktemplatelight

Returns the next-block template **without** the transaction list. The selected
transactions are cached server-side under `job_id`; a later `submitblocklight`
(pending) reconstructs the full block from the miner's solved header + coinbase
alone, so transactions never travel over the wire.

Response fields: `version`, `previousblockhash`, `height`, `coinbasevalue`
(subsidy + selected fees), `target`, `bits`, `mintime` (MTP + 1), `curtime`,
`sizelimit`, `sigchecklimit`, `noncerange`, `mutable`, `job_id`, and `merkle`.

`merkle` is the coinbase's merkle branch: the hashes, in display order like
`previousblockhash`, that the miner folds its coinbase txid with, one after the
other (`root = sha256d(root || hash)`, each hash in internal byte order), to get
the block's merkle root. It is empty when the template has no transactions.

Job cache config (mirrors bitcoind `-gbtcachesize` / `-gbtstoretime`):

```ini
rpc.gbt_cache_size = 10     # recent jobs kept
rpc.gbt_store_time = 3600   # seconds before a job expires
```

> The assembled template is cached in the core (`block_chain::fetch_mining_template`,
> shared by this RPC and the C-API): a new tip rebuilds immediately, while under
> mempool churn rebuilds are rate-limited to `chain.gbt_template_refresh_seconds`
> (default 5, bitcoind-style). `tools/rpc_bench.py` load-tests the miner-polling
> side; a faithful benchmark also simulates transactions and blocks arriving.

## Mining: getmininginfo

Returns a small mining snapshot: `blocks` (tip height), `difficulty` (of the next
required work, matching the template's `bits`), `pooledtx` (mempool size), `chain`
(the same short name as `getblockchaininfo`), and `warnings`. `networkhashps` is not reported yet — it needs a
historical hashrate estimate and is a follow-up.

## Mining: submitblocklight

`submitblocklight ["hexdata", "job_id"]` closes the light-template loop. `hexdata`
is the solved block carrying just the header and the coinbase; the node looks up
`job_id`, splices the cached selection after the coinbase, and submits the
reconstructed block to the chain. Returns the Bitcoin `submitblock` result: `null`
when accepted, otherwise a reject-reason string (e.g. `"duplicate block"`,
`"high-hash"`). Malformed input (missing args, bad hex, undecodable block, unknown
or expired `job_id`) is a JSON-RPC `-32602` error instead.
