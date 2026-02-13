# Sync Optimization - Pending Tasks

## Analysis Tools
1. **Actualizar `peer_performance.py`** - adaptarlo al formato de logs actual (no encontró datos de peers individuales en Run 10)

## Performance Optimizations
2. **Atomic counter optimization** - reemplazar vectores de slots por `next_chunk_.fetch_add(1)` para O(1) claims y menor memoria (~510KB → 8 bytes). Trade-off: necesita mecanismo separado para retry de chunks fallidos
3. **Reducir network pool threads** - la descarga es I/O-bound, 32 threads es overkill. Con 1 thread ya se logró ~93 MB/s. Probar con 4-8 threads y comparar throughput.
4. **Chunk timeout strategy** - muchos chunks hacen timeout a 15s y se resetean a FREE. Hay que diseñar una estrategia mejor: ¿reducir timeout? ¿penalizar peers lentos? ¿reasignar proactivamente? ¿tracking de qué peer tiene cada chunk?

## Peer Utilization
5. **Solo 17 de 32 peers descargando** - p2p_node reporta 32/32 peers conectados pero block_supervisor solo usa 17 para descargar. Investigar por qué ~15 peers no participan en la descarga. ¿Se filtran como slow? ¿No se les asignan chunks? ¿El peer_provider no los distribuye?

## Startup
6. **Startup time** - optimizar adquisición de peers (2.5 min de handshake)
7. **Low peer count al inicio** - throughput bajo con <14 peers, se estabiliza a 95-100 MB/s con 14+

## Cleanup
8. **Limpiar archivos temporales** - `task_status_*.txt`, csv, `chunk_coordinator copy.*`

## Header Index Persistence

12. **Persistir header_index completo en LMDB** — Actualmente LMDB solo guarda los datos del header (version, prev_hash, merkle_root, timestamp, bits, nonce). Los campos de `header_index` que refieren a flat files (`file_number`, `data_pos`, `undo_pos`, `status` flags como `have_data`) son solo in-memory y se pierden al reiniciar. Cada vez que se modifique `header_index` (set_block_pos, add_status, etc.) hay que escribir también a LMDB, y al cargar headers del DB hay que restaurar todos los campos. Cuando esto esté hecho, eliminar el workaround temporal de `scan_block_positions()` en `block_chain::start()`.

## Header Index Improvements

13. **Pre-computar MTP en header_index** — Actualmente el MTP (median time past) se calcula on-the-fly a partir de los últimos 11 timestamps. Agregar un campo `mtps_` (`std::vector<uint32_t>`) al header_index que almacene el MTP pre-computado para cada bloque. Se calcula una vez al cargar headers y se guarda/restaura desde LMDB junto con los demás campos del header. Beneficio: evita recalcular MTP cada vez que se necesita (UTXO build, validación, etc.).

## UTXO Build Pipeline

9. **Granularidad sub-bloque en checkpoint** — Actualmente `set_utxo_built_height()` guarda solo hasta qué bloque se procesó (cada 1000 bloques). Si se corta a mitad de un bloque, puede quedar inconsistente (UTXOs insertados pero deletes pendientes). Guardar hasta qué UTXO dentro del bloque (height:txid:vout) para recovery exacto sin duplicar ni perder operaciones.

10. **UTXO build en paralelo (pipeline)** — No esperar a que terminen los 930K bloques para arrancar el UTXO build. Mientras se escriben bloques a disco, un thread en paralelo va generando el UTXO set sobre los bloques ya almacenados. Producer-consumer: storage produce bloques listos, UTXO builder los consume.

11. **Write-Ahead Log (WAL) para UTXO operations** — Implementar un log de operaciones estilo WAL de bases de datos para recovery determinístico:
    - Registrar cada operación *antes* de ejecutarla
    - Formato:
      ```
      BEGIN_BLOCK 500001
      INSERT utxo height:txid:vout
      INSERT utxo ...
      DELETE utxo height:txid:vout
      DELETE utxo ...
      COMMIT_BLOCK 500001
      ```
    - En recovery: si hay un `BEGIN_BLOCK` sin `COMMIT_BLOCK`, rollback las operaciones parciales (o redo completo del bloque)
    - Permite recovery exacto sin perder ni duplicar trabajo
    - El WAL se puede truncar después de cada checkpoint exitoso

## Cleanup / Dead Code

14. **Eliminar `utxo_entry`** — Ahora el path de escritura usa `utxo_raw_value` (raw bytes directo a UTXO-Z). Actualizar el path de lectura (`bytes_to_entry` / `utxo_entry::from_data`) al nuevo formato (`height(4) + mtp(4) + coinbase(1) + raw_output`) y luego eliminar `utxo_entry` si ya no se usa en ningún lado.

15. **Eliminar código muerto de `utxo_builder`** — `process_block_utxos(domain::chain::block)`, `process_blocks_sequential`, `process_blocks_parallel`, `block_with_context`, `utxo_delta`, `apply_utxo_delta(internal_database)`. Todo el path basado en domain objects queda obsoleto con el zero-copy path.

16. **Eliminar código muerto de `internal_database`** — Funciones UTXO del viejo LMDB que ya no se usan (reemplazadas por UTXO-Z).

---

## Resueltos

- **RESUELTO: Toda la descarga corría en 1 solo thread** - El `io_context_` del executor tenía 1 solo thread (`io_thread_`). Se corrigió con `co_spawn` en el network pool. Ahora las coroutines se distribuyen en ~30 threads del pool (confirmado con thread_id logging).
- **RESUELTO: 100 threads en btop, pool fantasma** - Se descubrieron 3 threadpools de 32 threads: "network" (p2p_node), "priority" (block_chain) y "chain" (full_node). El pool "chain" se pasaba a block_chain como `threadpool& pool` pero nunca se usaba (32 threads desperdiciados). Se eliminó. Ahora: network (32) + priority (32) + io_thread (1) + main (1) + extras (~2) = 68 threads.
- **RESUELTO: Roller coaster de throughput** - Causado por round transitions en chunk_coordinator. Resuelto con slots=0 (1 slot por chunk). Run 10: 37 min, 93.1 MB/s avg, 0 drops, 81.9% del tiempo a 80+ MB/s.
