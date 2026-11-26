# Node logging

With `node.display = log`, the console shows operator-facing `info` messages,
plus all warnings, errors and critical failures. The debug log records `debug`
and higher. `log.verbose = true` (or `--log.verbose=true`) includes `trace` in both outputs.

## Console policy

- Keep startup, configuration, database recovery progress, listening endpoints
  and shutdown visible.
- Report connected-chain progress at most once every five seconds while batches
  advance: height, target header height, percentage, rate and estimated remaining
  time. The estimate follows the current header target; it is not a completion
  guarantee. No progress timer runs while the chain is idle.
- Report each new recent connected tip with its height and hash. Downloaded or
  stored bodies are not reported as a connected tip.
- Preserve warnings and failures, including rejected blocks and reorganization
  failures.

## Internal detail in the debug log

| Area | Detail kept off the normal console |
| --- | --- |
| Sync task lifecycle | Spawn/exit, channel closure, bridges and shutdown steps |
| Peers | Peer-provider changes, supervisor snapshots and address responses |
| Downloads | Per-peer timings, chunk failures/reclaims and throughput statistics |
| Storage | Chunk positions, fragmentation, header-index dumps and file allocation |
| Validation | Batch success, deferred signature counts and phase timings |
| UTXO construction | Each applied batch and BIP30 replacement details |
| Header management | Adopted/finalized header tips and repeated walk completion |
| Mempool persistence | Routine load/save transaction counts |

These are severity changes, not removal of the messages. Normal console progress
comes from the task publishing the connected UTXO state, rather than the faster
pipeline stages ahead of it. Existing legacy block-validation progress and
header-download progress remain visible.

## UTXO-Z reports

UTXO-Z 2.1.1 emits the statistics, sizing and height-range reports requested at
shutdown at `debug` level, including their headings and rotation-cause details.
The debug file retains these reports; the normal console omits them. `--log.verbose=true`
also shows them on the console. UTXO-Z's operational messages, warnings and
errors keep their existing levels.

The console/file sink thresholds, file format and configuration are unchanged.
