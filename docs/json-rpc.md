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
| `getblocktemplatelight` | `kth_chain_async_fetch_mining_template` _(pending)_ | `fetch_mining_template()` |
| `submitblocklight` | `kth_chain_async_organize_block` _(pending)_ | `organize(block)` |

_More methods land per phase: mining (`submitblock`, `getmininginfo`), mempool
(`getrawmempool`, `getmempoolentry`, ...), and blockchain/raw-tx (`getblock`,
`getrawtransaction`, `sendrawtransaction`, ...)._

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

> The template is rebuilt on every call (no per-tip cache yet). `tools/rpc_bench.py`
> load-tests the miner-polling side; a faithful benchmark also simulates
> transactions and blocks arriving (mempool churn + tip changes), which needs the
> submit paths added in later phases.

## Mining: submitblocklight

`submitblocklight ["hexdata", "job_id"]` closes the light-template loop. `hexdata`
is the solved block carrying just the header and the coinbase; the node looks up
`job_id`, splices the cached selection after the coinbase, and submits the
reconstructed block to the chain. Returns the Bitcoin `submitblock` result: `null`
when accepted, otherwise a reject-reason string (e.g. `"duplicate block"`,
`"high-hash"`). Malformed input (missing args, bad hex, undecodable block, unknown
or expired `job_id`) is a JSON-RPC `-32602` error instead.
