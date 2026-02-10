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

---

## Resueltos

- **RESUELTO: Toda la descarga corría en 1 solo thread** - El `io_context_` del executor tenía 1 solo thread (`io_thread_`). Se corrigió con `co_spawn` en el network pool. Ahora las coroutines se distribuyen en ~30 threads del pool (confirmado con thread_id logging).
- **RESUELTO: 100 threads en btop, pool fantasma** - Se descubrieron 3 threadpools de 32 threads: "network" (p2p_node), "priority" (block_chain) y "chain" (full_node). El pool "chain" se pasaba a block_chain como `threadpool& pool` pero nunca se usaba (32 threads desperdiciados). Se eliminó. Ahora: network (32) + priority (32) + io_thread (1) + main (1) + extras (~2) = 68 threads.
- **RESUELTO: Roller coaster de throughput** - Causado por round transitions en chunk_coordinator. Resuelto con slots=0 (1 slot por chunk). Run 10: 37 min, 93.1 MB/s avg, 0 drops, 81.9% del tiempo a 80+ MB/s.
