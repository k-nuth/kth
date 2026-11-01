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
| `getblockchaininfo` | `kth_chain_sync_mining_info` + `kth_chain_sync_block_hash` | `fetch_mining_info()` + `get_last_heights()` |
| `getrawtransaction` | `kth_chain_sync_transaction` (+ `kth_chain_sync_block_by_hash`) | `fetch_transaction(hash)` (mempool), or `fetch_block(blockhash)` |
| `getblock` | `kth_chain_sync_block_by_hash` | `fetch_block(hash)` (flat-file block store) |
| `getblockheader` (`verbose=false`: raw 80-byte header hex) | `kth_chain_sync_block_header_by_hash` | `fetch_block_header(hash)` |
| `sendrawtransaction` | `kth_chain_sync_organize_transaction` | `organize(tx)` |
| `decoderawtransaction` | `kth_chain_transaction_construct_from_data` | `transaction::from_data()` |
| `getrawmempool` | `kth_chain_get_mempool_txids` | `get_mempool_txids()` |
| `getmempoolinfo` | `kth_chain_get_mempool_info` | `get_mempool_info()` |
| `getmempoolentry` | `kth_chain_get_mempool_entry` | `get_mempool_entry()` (+ depends/spentby) |
| `getmempoolancestors` | `kth_chain_get_mempool_ancestors` | `get_mempool_ancestors()` |
| `getmempooldescendants` | `kth_chain_get_mempool_descendants` | `get_mempool_descendants()` |
| `getblocktemplatelight` | `kth_chain_sync_mining_template` / `kth_chain_async_mining_template` | `fetch_mining_template()` |
| `getmininginfo` | `kth_chain_sync_mining_info` / `kth_chain_async_mining_info` | `fetch_mining_info()` |
| `submitblocklight` | `kth_chain_sync_organize_block` / `kth_chain_async_organize_block` | `organize(block)` |

Each async chain reader is exposed in both a blocking (`kth_chain_sync_*`) and a
callback (`kth_chain_async_*`) flavor.

_More methods land per phase: mining (`submitblock`, `getmininginfo`), mempool
(`getrawmempool`, `getmempoolentry`, ...), and blockchain/raw-tx (`getblock`,
`getrawtransaction`, `sendrawtransaction`, ...)._

## Blocks and transactions

`getblock "<hash>" [verbosity=1]` reads the block from the flat-file block
store, through the header index; a side-branch block the node holds is returned
too. Verbosity `0` (or `false`) returns it as hex. `1` (or `true`) returns `hash`,
`confirmations` (-1 off the connected chain), `size`, `height`, `version`,
`versionHex`, `merkleroot`, `tx` (txids), `time`, `mediantime`, `nonce`, `bits`,
`difficulty`, `chainwork`, `nTx`, `previousblockhash` and `nextblockhash`. `2`
lists each transaction with its `decoderawtransaction` fields and `hex`. `fee`
per transaction and `ablastate` are not reported. `getrawtransaction "<txid>"` answers for a mempool transaction.
Confirmed transactions are not indexed by txid (#491), so for one of those pass
the block hash as the third parameter: `getrawtransaction "<txid>" false
"<blockhash>"`. With `verbose` true (or non-zero) it returns the
`decoderawtransaction` fields with `hex`, and for a transaction read from a block
also `blockhash`, `confirmations`, `time`, `blocktime` and `in_active_chain`; for a
block off the connected chain `confirmations` is 0, the times are left out and
`in_active_chain` is false. A block or transaction the node does not have is error `-5`.

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

## Mining: getblocktemplatelight

Returns the next-block template **without** the transaction list. The selected
transactions are cached server-side under `job_id`; a later `submitblocklight`
(pending) reconstructs the full block from the miner's solved header + coinbase
alone, so transactions never travel over the wire.

Response fields: `version`, `previousblockhash`, `height`, `coinbasevalue`
(subsidy + selected fees), `target`, `bits`, `mintime` (MTP + 1), `curtime`,
`sizelimit`, `sigchecklimit`, `noncerange`, `mutable`, and `job_id`.

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
(network name), and `warnings`. `networkhashps` is not reported yet — it needs a
historical hashrate estimate and is a follow-up.

## Mining: submitblocklight

`submitblocklight ["hexdata", "job_id"]` closes the light-template loop. `hexdata`
is the solved block carrying just the header and the coinbase; the node looks up
`job_id`, splices the cached selection after the coinbase, and submits the
reconstructed block to the chain. Returns the Bitcoin `submitblock` result: `null`
when accepted, otherwise a reject-reason string (e.g. `"duplicate block"`,
`"high-hash"`). Malformed input (missing args, bad hex, undecodable block, unknown
or expired `job_id`) is a JSON-RPC `-32602` error instead.
