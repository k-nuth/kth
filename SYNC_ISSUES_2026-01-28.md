# Sync Issues - 2026-01-28

## Estado General

Trabajando en el nuevo sistema de sync CSP-based. Múltiples problemas identificados durante testing.

---

## Problemas Supuestamente Solucionados (pendiente verificación)

### 1. Header sync loop infinito con mismo peer fallido

**Síntoma:**
```
[header_sync] Peer 161.97.72.190:8333 ABC:0.27.5 failed: connection timed out
[header_sync] Using peer: 161.97.72.190:8333 ABC:0.27.5   <-- mismo peer!
```
Repetía cada 30 segundos indefinidamente, ignorando 7-8 peers disponibles.

**Causa raíz:** Race condition entre mensajes `peers_updated` y peer failures.
1. peer_provider hace broadcast de peers (peer X en lista)
2. header_download espera 30s por respuesta de peer X
3. Mientras tanto, nuevos peers conectan → peer_provider hace broadcast de nuevo (X sigue en lista)
4. Timeout termina, X falla → `last_failed_nonce = X`
5. header_download procesa el `peers_updated` viejo de la cola
6. Código viejo: `last_failed_nonce.reset()` incondicional ← BUG
7. header_download selecciona X de nuevo (sigue en la lista vieja)

**Fix:** Solo limpiar `last_failed_nonce` si el peer fallido NO está en la nueva lista.

**Archivo:** `src/node/src/sync/header_tasks.cpp:179-220`

---

### 2. "Missing parent" baneando peers BCHN inocentes

**Síntoma:**
```
[validate_header] Missing parent at height 663648:
  header.prev_hash=00000000000000000083ed4b7a780d59e3983513215518ad75654bb02deee62f
  expected parent_hash=00000000000000000172e9f83e9d3a9e511af16efcfaecd3eefbf51595ad5cd5
[peer_provider] Banning peer 3.211.190.198:8333 BCHN:28.0.1 for invalid data: block missing parent
```
Después de que ABC falla checkpoint en height 661648, TODOS los peers BCHN subsiguientes eran baneados.

**Causa raíz:** Retry requests duplicados del coordinator.
1. ABC peer falla checkpoint → coordinator envía retry request #1
2. ABC peer es baneado, channel se cierra → header_download reporta failure
3. Coordinator recibe failure → envía retry request #2
4. BCHN peer recibe request #1, envía headers 661648-663647 (éxito)
5. Request #2 llega → BCHN peer envía headers desde 661648 de nuevo
6. Pero organizer ya está en tip=663647, espera headers desde 663648
7. Header entrante tiene prev_hash=661647, organizer espera prev_hash=663647
8. → "missing parent" → BCHN peer baneado injustamente

**Fix:** En `header_organizer::add_headers()`, detectar si los headers conectan a un bloque anterior al tip actual. Si es así, son headers stale/duplicados y se saltan silenciosamente.

**Archivo:** `src/blockchain/src/pools/header_organizer.cpp:84-119`

**Log esperado después del fix:**
```
[header_organizer] Skipping stale header batch: first header connects to height 661647 but tip is at height 663647 - likely duplicate retry request
```

---

### 3. Timing measurements para block storage

**Síntoma:** No había visibilidad de performance del sistema de almacenamiento de bloques.

**Fix:** Agregados stats atómicos para medir:
- `allocate_calls/time/bytes` - pre-alocación de archivos (posix_fallocate)
- `write_block_calls/time/bytes` - escritura de bloques
- `file_open_calls/time` - apertura de archivos

**Archivos modificados:**
- `src/infrastructure/include/kth/infrastructure/utility/stats.hpp`
- `src/database/src/flat_file_seq.cpp`
- `src/database/src/block_store.cpp`
- `src/node/src/sync/block_tasks.cpp`

**Log esperado:**
```
[block_storage] alloc: n=1 avg=25.3ms total=128.0MB | write: n=500 avg=0.15ms 85.2MB/s | open: n=501 avg=42.1us
```

---

## Problemas Pendientes de Verificar

### 4. Segfault al hacer Ctrl-C durante sync

**Síntoma:**
```
^C[node] Stop signal detected (code: 2).
[node] Please wait while the node is stopping...
[header_sync] Peer X failed: channel stopped
[1]    731585 segmentation fault (core dumped)
```

**Estado:** Agregados checkpoints de debug para identificar línea exacta del crash.

**Archivo:** `src/node/src/sync/header_tasks.cpp:97-126`

**Checkpoints agregados:**
```
[header_sync] checkpoint 1: after log
[header_sync] checkpoint 2: got nonce X
[header_sync] checkpoint 3: cleared current_peer
[header_sync] checkpoint 4: got error code
[header_sync] checkpoint 5: try_send returned X
[header_sync] checkpoint 6: returning from error handler
```

**Próximo paso:** Ver cuál es el último checkpoint antes del crash para identificar la operación que causa el segfault.

---

### 5. Segfault aleatorio durante block sync

**Síntoma:** Segfault que ocurre "de la nada" durante la sincronización de bloques (no relacionado con Ctrl-C).

**Estado:** Pendiente de reproducir y diagnosticar con ASAN.

**Notas:**
- Ocurría antes cuando se corría con ASAN pero iba muy lento
- Posiblemente relacionado con el sistema de almacenamiento de bloques o UTXO
- Podría ser memory corruption en algún lugar del pipeline

---

### 6. Shutdown hang (2 peer tasks no completan)

**Síntoma:**
```
[p2p_node] Waiting for peer_tasks.join() - if this hangs, check which peer task didn't complete
```
El proceso queda colgado porque `peer_tasks.join()` espera indefinidamente. De 8 tasks activas, solo 6 completaban.

**Estado:** Agregado logging para identificar qué peers quedan colgados.

**Archivo:** `src/network/src/p2p_node.cpp`

**Posible causa:** Uso de operador `||` con channels/timers que causa que algunas coroutines no terminen correctamente.

---

### 7. Pre-allocation optimization (128MB chunks)

**Cambio:** `BLOCKFILE_CHUNK_SIZE` de 16MB a 128MB para reducir cantidad de syscalls durante IBD.

**Archivo:** `src/database/include/kth/database/flat_file_seq.hpp`

**Estado:** Pendiente de probar rendimiento. Los nuevos stats de timing ayudarán a evaluar el impacto.

---

### 8. Segfault durante header sync

**Síntoma:** Segfault espontáneo durante sincronización de headers, alrededor de height 424000.

**Último log antes del crash:**
```
[header_sync] Progress: 424000 headers (30000 headers/s)
[peer_session] Read error [3.228.193.
```
El log se trunca a mitad de línea - el crash ocurrió durante la operación de logging de un error de lectura de peer.

**Estado:** Checkpoints agregados, pendiente de verificar.

**Notas:**
- Diferente del segfault de Ctrl-C (#4) - este ocurre espontáneamente durante sync normal
- El crash parece ocurrir en `peer_session` durante el manejo de un error de lectura
- Velocidad de sync era normal (~30000 headers/s) antes del crash
- Posible race condition o use-after-free en el manejo de errores de peer

**Checkpoints agregados en `src/network/src/peer_session.cpp:393-407`:**
```
[peer_session] read_loop checkpoint 1: read_message returned error
[peer_session] read_loop checkpoint 2: got error code X
[peer_session] read_loop checkpoint 3: got authority X.X.X.X:8333
[peer_session] read_loop checkpoint 4: got error message ...
[peer_session] Read error [X.X.X.X:8333]: ...
[peer_session] read_loop checkpoint 5: logged error, calling stop()
[peer_session] read_loop checkpoint 6: stop() returned, co_return
```

**Próximos pasos:**
- Ver cuál es el último checkpoint antes del crash
- Correr con ASAN para detectar memory corruption
- Revisar si hay shared state que no esté protegido correctamente

---

### 9. Block sync usando solo 1 peer (nuevo)

**Síntoma:** De 13 peers que inician block download tasks, 12 terminan en menos de 1 segundo, dejando solo 1 peer activo.

**Log relevante:**
```
[block_supervisor] Spawning tasks for 13 buffered peers
[block_download] Task started for peer 162.55.69.229:8333 BCHN:28.0.1 (active peers: 1)
[block_download] Task started for peer 63.250.53.156:8333 ABC:0.32.3 (active peers: 2)
... (13 peers spawned)
[block_download] Peer 89.169.30.178:8333 BCHN:28.0.1 failed to download chunk 3: channel stopped
[block_download] Task ended for peer 89.169.30.178:8333 BCHN:28.0.1 (downloaded 0 chunks, active peers: 12)
... (todos los peers terminan en <1 segundo)
[block_download] Task ended for peer 185.207.251.50:8333 ABC:0.32.5 (downloaded 4 chunks, active peers: 1)
```
Después de 0.7 segundos, solo queda `14.65.10.55:8333` descargando bloques.

**Estado:** Nuevo, pendiente de investigar.

**Posibles causas:**
- Los tasks terminan después de descargar pocos chunks (0-4) en vez de continuar
- Posible bug en el loop de `block_download_task` que sale prematuramente
- Condición de salida incorrecta en `coordinator->is_complete()` o `coordinator->is_stopped()`

**Archivo:** `src/node/src/sync/block_tasks.cpp:126-140`

---

### 10. Segfault durante Ctrl-C en block sync (nuevo)

**Síntoma:** Segfault al hacer Ctrl-C durante la sincronización de bloques.

**Último log antes del crash:**
```
[peer_session] Stopping session [31.220.93.253:8333]: channel stoppe
```
El log se trunca a mitad de palabra ("stoppe" en vez de "stopped").

**Estado:** Nuevo, pendiente de investigar.

**Notas:**
- Los checkpoints de `read_loop` pasan todos (1-6) para varios peers antes del crash
- El crash parece ocurrir en otra parte del shutdown, no en `read_loop`
- Diferente del segfault #4 (header sync Ctrl-C) y #8 (header sync espontáneo)
- 14 peers estaban conectados al momento del Ctrl-C

**Próximos pasos:**
- Agregar checkpoints en el shutdown path de `peer_session::stop()`
- Revisar `peer_manager::remove()` y el cleanup de peers durante shutdown

---

## Archivos Modificados en Esta Sesión

| Archivo | Cambios |
|---------|---------|
| `src/node/src/sync/header_tasks.cpp` | Fix loop infinito + checkpoints debug segfault |
| `src/blockchain/src/pools/header_organizer.cpp` | Fix missing parent con headers stale |
| `src/infrastructure/include/kth/infrastructure/utility/stats.hpp` | Nuevos campos para block storage stats |
| `src/database/src/flat_file_seq.cpp` | Timing para allocate y file_open |
| `src/database/src/block_store.cpp` | Timing para write_block |
| `src/node/src/sync/block_tasks.cpp` | Output periódico de storage stats |
| `src/database/include/kth/database/flat_file_seq.hpp` | BLOCKFILE_CHUNK_SIZE 16MB→128MB |
| `src/network/src/p2p_node.cpp` | Debug logging para shutdown |
| `src/network/src/peer_session.cpp` | Checkpoints debug para segfault en read_loop |

---

## Resumen de Segfaults

| # | Contexto | Trigger | Estado |
|---|----------|---------|--------|
| 4 | Header sync | Ctrl-C | Checkpoints en header_tasks.cpp |
| 8 | Header sync | Espontáneo | Checkpoints en peer_session.cpp |
| 10 | Block sync | Ctrl-C | Nuevo - crash en shutdown |

---

## Notas Adicionales

- Los comentarios en el código incluyen la fecha (2026-01-28) y descripción del síntoma para evitar ping-pong entre soluciones cuando se pierde contexto.
- El fork BCH/ABC ocurre en height 661648 (Euler activation). Los checkpoints están configurados correctamente en heights 661646, 661647, 661648.
