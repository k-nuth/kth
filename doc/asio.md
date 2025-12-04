# PLAN DE MIGRACIÓN A ASIO MODERNO CON COROUTINES

## Diagramas de Arquitectura

### Arquitectura Actual (Callback-based)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              FULL_NODE                                       │
│                           (src/node/)                                        │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                                 P2P                                          │
│                          (src/network/p2p.hpp)                               │
│                                                                              │
│  ┌─────────────────┐  ┌──────────────────┐  ┌──────────────────────────┐   │
│  │ pending_connect │  │ pending_handshake│  │     pending_close        │   │
│  │  (connectors)   │  │    (channels)    │  │ (established channels)   │   │
│  └─────────────────┘  └──────────────────┘  └──────────────────────────┘   │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │                        threadpool + dispatcher                       │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
        ┌──────────────────────────┼──────────────────────────┐
        │                          │                          │
        ▼                          ▼                          ▼
┌───────────────┐        ┌───────────────┐        ┌────────────────────┐
│ session_seed  │        │session_inbound│        │  session_outbound  │
│               │        │               │        │                    │
│ ┌───────────┐ │        │ ┌───────────┐ │        │  ┌──────────────┐  │
│ │ connector │ │        │ │ acceptor  │ │        │  │session_batch │  │
│ └─────┬─────┘ │        │ └─────┬─────┘ │        │  │              │  │
│       │       │        │       │       │        │  │ ┌──────────┐ │  │
│       ▼       │        │       ▼       │        │  │ │connector │ │  │
│  ┌─────────┐  │        │  ┌─────────┐  │        │  │ └────┬─────┘ │  │
│  │ channel │  │        │  │ channel │  │        │  │      ▼       │  │
│  └────┬────┘  │        │  └────┬────┘  │        │  │ ┌─────────┐  │  │
│       │       │        │       │       │        │  │ │ channel │  │  │
└───────┼───────┘        └───────┼───────┘        │  │ └────┬────┘  │  │
        │                        │                │  └──────┼──────┘  │
        │                        │                └─────────┼─────────┘
        │                        │                          │
        └────────────────────────┴──────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              CHANNEL                                         │
│                       (src/network/channel.hpp)                              │
│                                                                              │
│    ┌─────────────────────────────────────────────────────────────────────┐  │
│    │                           PROXY                                      │  │
│    │                    (src/network/proxy.hpp)                           │  │
│    │                                                                      │  │
│    │   ┌──────────────┐    ┌──────────────┐    ┌──────────────────────┐  │  │
│    │   │   socket     │    │  sequencer   │    │ message_subscriber   │  │  │
│    │   │  (mutex)     │    │ (write queue)│    │    (callbacks)       │  │  │
│    │   └──────────────┘    └──────────────┘    └──────────────────────┘  │  │
│    │                                                                      │  │
│    │   read_heading() ──► handle_read_heading() ──► read_payload() ──►   │  │
│    │   handle_read_payload() ──► dispatch to subscribers ──► loop        │  │
│    │                                                                      │  │
│    └─────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│    ┌──────────────┐    ┌──────────────┐    ┌──────────────────────────┐    │
│    │   deadline   │    │   deadline   │    │       nonce/version      │    │
│    │ (inactivity) │    │ (expiration) │    │     (peer metadata)      │    │
│    └──────────────┘    └──────────────┘    └──────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            PROTOCOLS                                         │
│                     (src/network/protocols/)                                 │
│                                                                              │
│   protocol (base)                                                            │
│   ├── protocol_events                                                        │
│   │   ├── protocol_timer                                                     │
│   │   │   ├── protocol_version_31402 ──► protocol_version_70002             │
│   │   │   ├── protocol_ping_31402 ──► protocol_ping_60001                   │
│   │   │   ├── protocol_address_31402                                         │
│   │   │   └── protocol_seed_31402                                            │
│   │   └── protocol_reject_70002                                              │
│                                                                              │
│   Usan macros: BIND1-7, SEND1-3, SUBSCRIBE2-3                               │
│   Callbacks encadenados: send() → handle_send() → subscribe() → ...         │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Arquitectura Nueva (Coroutine-based) - En Progreso

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              FULL_NODE                                       │
│                           (src/node/)                                        │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                              P2P_NODE                                        │
│                      (src/network/p2p_node.hpp) [FASE 6]                     │
│                                                                              │
│    ┌────────────────────────────────────────────────────────────────────┐   │
│    │                      asio::thread_pool                             │   │
│    └────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│    ┌────────────────────┐              ┌────────────────────────────────┐   │
│    │    peer_manager    │◄────────────►│   concurrent_channel<events>   │   │
│    │   [FASE 4] ✓       │              │         [FASE 1] ✓             │   │
│    └─────────┬──────────┘              └────────────────────────────────┘   │
│              │                                                               │
└──────────────┼───────────────────────────────────────────────────────────────┘
               │
    ┌──────────┴──────────┐
    │                     │
    ▼                     ▼
┌──────────┐        ┌───────────────┐
│ Outbound │        │    Inbound    │
│          │        │               │
│ co_await │        │   co_await    │
│ connect  │        │    accept     │
│ [FASE 3] │        │   [FASE 3]    │
└────┬─────┘        └───────┬───────┘
     │                      │
     └──────────┬───────────┘
                │
                ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           PEER_SESSION                                       │
│                    (src/network/peer_session.hpp) [FASE 2] ✓                 │
│                                                                              │
│    ┌─────────────────────────────────────────────────────────────────────┐  │
│    │                         asio::strand                                 │  │
│    │                    (serializa acceso al socket)                      │  │
│    └─────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│    ┌───────────────────┐    ┌───────────────────┐    ┌──────────────────┐  │
│    │  tcp::socket      │    │ concurrent_channel │    │  steady_timer    │  │
│    │                   │    │    (outbound_)     │    │  (inactivity)    │  │
│    └───────────────────┘    └───────────────────┘    └──────────────────┘  │
│                                                                              │
│    co_await run() ───────────────────────────────────────────────────────►  │
│    │                                                                         │
│    ├── co_await read_loop()   ──► async_read(heading) ──► async_read(payload)│
│    ├── co_await write_loop()  ──► channel.receive() ──► async_write()       │
│    ├── co_await inactivity()  ──► timer.wait() ──► cancel or timeout        │
│    └── co_await expiration()  ──► timer.wait() ──► session expires          │
│                                                                              │
│    Todas corren en paralelo con || operator, cancelación automática         │
└─────────────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PROTOCOLS (Coroutine-based)                          │
│                     (src/network/protocols/) [FASE 5]                        │
│                                                                              │
│   awaitable<code> handshake() {                                              │
│       co_await send(version_msg);          // Lineal, sin callbacks          │
│       auto ver = co_await receive<version>();                                │
│       co_await send(verack_msg);                                             │
│       co_await receive<verack>();                                            │
│       co_return success;                                                     │
│   }                                                                          │
│                                                                              │
│   Sin macros BIND, sin callback chains, código lineal y legible             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Clases a Eliminar vs. Reemplazo

```
CLASES LEGACY (a eliminar)              REEMPLAZO MODERNO
═══════════════════════════════════════════════════════════════════════════════

┌───────────────────────────┐           ┌───────────────────────────────────┐
│      infrastructure/      │           │                                   │
├───────────────────────────┤           │                                   │
│ threadpool.hpp     ────────────────►  │  asio::thread_pool (built-in)    │
│ work.hpp           ────────────────►  │  co_await post(executor)         │
│ sequencer.hpp      ────────────────►  │  asio::strand (built-in)         │
│ dispatcher.hpp     ────────────────►  │  co_spawn + strand               │
│ synchronizer.hpp   ────────────────►  │  awaitable operators (|| &&)     │
│ subscriber.hpp     ────────────────►  │  concurrent_channel              │
│ resubscriber.hpp   ────────────────►  │  concurrent_channel + retry      │
│ delegates.hpp      ────────────────►  │  Lambdas o eliminado             │
│ deadline.hpp       ────────────────►  │  asio::steady_timer directo      │
│ socket.hpp (mutex) ────────────────►  │  Socket en strand, sin mutex     │
└───────────────────────────┘           │                                   │
                                        └───────────────────────────────────┘

┌───────────────────────────┐           ┌───────────────────────────────────┐
│        network/           │           │                                   │
├───────────────────────────┤           │                                   │
│ proxy.hpp          ────────────────►  │  peer_session (coroutine loops)  │
│ channel.hpp        ────────────────►  │  peer_session                    │
│ connector.hpp      ────────────────►  │  async_connect() helper o inline │
│ acceptor.hpp       ────────────────►  │  async_accept() helper o inline  │
│ pending.hpp        ────────────────►  │  std::unordered_map + strand     │
│ message_subscriber ────────────────►  │  concurrent_channel<raw_message> │
└───────────────────────────┘           │                                   │
                                        └───────────────────────────────────┘
```

### Estado de la Migración

```
═══════════════════════════════════════════════════════════════════════════════
                           ESTADO ACTUAL DE FASES
═══════════════════════════════════════════════════════════════════════════════

[✓] FASE 0: Preparación
    └── C++23, standalone Asio 1.36, verificado

[✓] FASE 1: Infraestructura Base
    ├── concurrent_channel probado y documentado
    ├── Lección aprendida: channel vs concurrent_channel
    └── awaitable_helpers disponibles

[✓] FASE 2: peer_session (PR #142)
    ├── peer_session.hpp creado
    ├── peer_session.cpp implementado
    ├── Tests unitarios: 31 test cases, 91 assertions ✓
    └── Reemplaza: proxy + channel

[✓] FASE 3: Connector/Acceptor → Helpers (PR #142)
    ├── async_connect(): DNS + TCP connect con timeout
    ├── async_accept(): acepta conexiones entrantes
    ├── async_listen(): crea acceptor en puerto
    └── Tests incluidos en peer_session tests

[✓] FASE 4: Session Management (PR #144)
    ├── peer_manager.hpp/cpp creado
    ├── Reemplaza: pending<connector>/pending<channel> collections
    ├── Usa asio::strand para serialización (sin mutex)
    ├── Todas las operaciones son coroutines (asio::awaitable)
    ├── broadcast(), for_each(), add/remove/find operations
    └── Tests unitarios completos

[~] FASE 5: Protocols con Coroutines (PARCIAL)
    ├── ✅ protocols_coro.hpp creado
    ├── ✅ perform_handshake() - versión/verack
    ├── ✅ run_ping_pong() - keepalive
    ├── ✅ request_addresses() / send_addresses()
    ├── ❌ FALTA: Block sync (getblocks, inv, getdata, block)
    ├── ❌ FALTA: Header sync (getheaders, headers)
    ├── ❌ FALTA: Transaction relay (inv, getdata, tx, mempool)
    └── ❌ FALTA: Double spend proofs (BCH específico)

[✓] FASE 6: P2P Node Modernizado
    ├── p2p_node.hpp/cpp creado
    ├── full_node usa composición con p2p_node (HAS-A en lugar de IS-A)
    └── Conexión básica funciona (handshake + ping/pong)

[✓] FASE 7: Cleanup (COMPLETA - Red P2P funcional)
    ├── ✅ node::sessions eliminados (inbound, outbound, manual, block_sync, header_sync)
    ├── ✅ node::protocols eliminados (block_in/out, header_sync, tx_in/out, dsp_in/out)
    ├── ✅ network::sessions COMENTADOS (session.cpp, session_*.cpp)
    ├── ✅ network::protocols legacy COMENTADOS (protocol_*.cpp)
    ├── ✅ network::core legacy COMENTADOS (p2p.cpp, proxy.cpp, channel.cpp, acceptor.cpp, connector.cpp, message_subscriber.cpp)
    ├── ✅ test/p2p.cpp COMENTADO (TODO: migrar tests a p2p_node.cpp)
    ├── ✅ VERIFICADO EN RED REAL:
    │   ├── Seeding funciona (obtiene 1000+ addresses)
    │   ├── Conexiones outbound funcionan
    │   ├── Handshake completo (version/verack)
    │   ├── Ping/pong funciona correctamente
    │   └── Recepción de mensajes (inv, addr, getheaders, feefilter, etc.)
    ├── ⏳ c-api comentado temporalmente (reactivar cuando termine cleanup)
    └── ⏳ infrastructure legacy (sequencer, dispatcher, etc.) - pendiente

[ ] FASE 8: Integration Tests
    └── Tests de conexión real a la red (requiere nodo limpio)

═══════════════════════════════════════════════════════════════════════════════
```

---

Now I have a comprehensive view of the architecture. Let me provide you with a detailed analysis report.

  ---
  Comprehensive Boost Asio Architecture Review

  Executive Summary

  The codebase implements a well-structured, layered async I/O architecture using Boost/standalone Asio. The fundamental design is
  architecturally sound with no critical flaws, but there are several areas for improvement and modernization opportunities.

  ---
  1. Architecture Overview

  The architecture follows a layered design:

  ┌─────────────────────────────────────────────────────┐
  │                    full_node                        │
  │                 (src/node/full_node.hpp)            │
  ├─────────────────────────────────────────────────────┤
  │                      p2p                            │
  │                (src/network/p2p.hpp)                │
  ├─────────────────────────────────────────────────────┤
  │    Sessions (inbound, outbound, manual, seed)       │
  │              (src/network/sessions/)                │
  ├─────────────────────────────────────────────────────┤
  │  Protocols (version, ping, address, reject, etc.)   │
  │              (src/network/protocols/)               │
  ├─────────────────────────────────────────────────────┤
  │      channel → proxy → socket                       │
  │         (src/network/channel.hpp, proxy.hpp)        │
  ├─────────────────────────────────────────────────────┤
  │    Infrastructure (threadpool, work, dispatcher)    │
  │          (src/infrastructure/utility/)              │
  └─────────────────────────────────────────────────────┘

  ---
  2. Key Components Analysis

  2.1 Thread Pool & Work Management (threadpool.hpp, work.hpp)

  Strengths:
  - Uses modern io_context and executor_work_guard pattern (post-Boost 1.66)
  - Proper use of asio::post() for async dispatch
  - Four dispatch patterns: bound(), concurrent(), ordered(), unordered()

  Issues Found:
  - Issue #76 TODO at work.hpp:65-66: The code uses bind_executor but has a TODO to review behavioral differences with deprecated
  wrap(). This should be validated.

  // work.hpp:65-67
  // TODO: Review bind_executor vs deprecated wrap() for behavioral differences
  // See: https://github.com/k-nuth/kth-mono/issues/76
  ::asio::post(service_, ::asio::bind_executor(strand_, ...));

  2.2 Sequencer (sequencer.cpp)

  Strengths:
  - Correct FIFO ordering for async operations
  - Proper mutex-protected queue
  - Clean lock/unlock API for sequential async chains

  Correct Pattern:
  void sequencer::lock(action&& handler) {
      {
          unique_lock locker(mutex_);
          if (executing_) {
              actions_.push(std::move(handler));
              return;
          }
          executing_ = true;
      }
      ::asio::post(service_, std::move(handler));
  }

  2.3 Proxy (proxy.hpp, proxy.cpp)

  Strengths:
  - Good separation of read/write cycles
  - Proper use of dispatch_.lock() for write serialization (prevents deadlocks)
  - Atomic stopped_ flag for safe concurrent access

  Read Cycle (Correct):
  read_heading() → handle_read_heading() → read_payload() → handle_read_payload() → loop

  Write Cycle (Correct):
  send() → dispatch_.lock() → do_send() → async_write → handle_send() → dispatch_.unlock()

  2.4 Synchronizer (synchronizer.hpp)

  Strengths:
  - Clean join pattern for coordinating multiple async operations
  - Three termination modes: on_error, on_success, on_count
  - Thread-safe with upgrade_mutex

  Potential Issue:
  // synchronizer.hpp:100-105
  mutex_->unlock_upgrade_and_lock();
  (*counter_) = count;
  mutex_->unlock();
  The upgrade-to-exclusive pattern is correct but could potentially cause priority inversion under heavy contention.

  2.5 Pending Collections (pending.hpp)

  Strengths:
  - Thread-safe container for managing connections
  - Proper use of upgrade_mutex for read-heavy operations
  - Clean stop/close semantics

  ---
  3. Concurrency Analysis

  3.1 Thread Safety Patterns Used

  | Pattern       | Location                                         | Status    |
  |---------------|--------------------------------------------------|-----------|
  | Atomic flags  | proxy::stopped_, channel::notify_, p2p::stopped_ | ✅ Correct |
  | Upgrade mutex | acceptor, connector, pending, synchronizer       | ✅ Correct |
  | Strands       | work::strand_                                    | ✅ Correct |
  | Sequencer     | Write operations in proxy                        | ✅ Correct |

  3.2 Potential Race Conditions

  No critical races found, but some areas need attention:

  1. channel.cpp:97 - Comment says "inherent races, requiring stranding by stop only":
  // Timers (these are inherent races, requiring stranding by stop only).
  void channel::start_expiration() {
      if (proxy::stopped()) { return; }
      expiration_->start(...);
  }
  This is a benign race - if stopped, the timer handler will return early.

  2. acceptor.cpp:121 - TODO about accept thread:
  // TODO: if the accept is invoked on a thread of the acceptor, as opposed
  // to the thread of the socket, then this is unnecessary.

  ---
  4. Architecture Observations

  4.1 Good Practices Found

  1. enable_shared_from_base<> - Proper pattern for safe async callbacks with inheritance
  2. RAII assertions - Destructors assert proper cleanup (KTH_ASSERT_MSG)
  3. Consistent use of shared_ptr for async lifetime management
  4. Clear separation between connection management (pending_connect, pending_handshake, pending_close)

  4.2 Areas for Improvement

  1. Excessive callback chains - The current pattern uses deeply nested std::bind chains:
  // Example from session.cpp
  channel->start(BIND3(handle_starting, _1, channel, handle_started));
  This is hard to follow and error-prone.

  2. Manual memory management with std::bind - Error-prone, could be simplified.
  3. Commented-out code - Several files have extensive commented code blocks (e.g., dispatcher.hpp:142-206).
  4. Macros for binding - BIND1-7, SEND1-3, SUBSCRIBE2-3 macros are fragile and hard to debug.

  ---
  5. Modernization Opportunities

  5.1 Coroutine Migration Path (C++20)

  High Impact, Moderate Effort:

  The current callback-based design can be migrated to coroutines. Here's a comparison:

  Current Pattern (callback hell):
  void connector::connect(std::string const& hostname, uint16_t port, connect_handler handler) {
      mutex_.lock_upgrade();
      if (stopped()) {
          mutex_.unlock_upgrade();
          dispatch_.concurrent(handler, error::service_stopped, nullptr);
          return;
      }
      mutex_.unlock_upgrade_and_lock();
      resolver_.async_resolve(hostname, std::to_string(port),
          std::bind(&connector::handle_resolve, shared_from_this(), _1, _2, handler));
      mutex_.unlock();
  }

  void connector::handle_resolve(boost_code const& ec, asio::results_type results, connect_handler handler) {
      // ... more callbacks
  }

  With C++20 Coroutines:
  asio::awaitable<channel::ptr> connector::connect(std::string const& hostname, uint16_t port) {
      auto guard = co_await mutex_.async_lock();
      if (stopped()) {
          co_return nullptr;
      }

      auto results = co_await resolver_.async_resolve(hostname, std::to_string(port), asio::use_awaitable);
      auto socket = std::make_shared<kth::socket>(pool_);

      // Timer race
      using namespace asio::experimental::awaitable_operators;
      auto result = co_await (
          async_connect(socket->get(), results, asio::use_awaitable) ||
          timer_->async_wait(asio::use_awaitable)
      );

      if (result.index() == 1) {  // timer won
          co_return nullptr;
      }

      co_return std::make_shared<channel>(pool_, socket, settings_);
  }

  Benefits:
  - Eliminates callback chains
  - Better error handling with exceptions
  - Cleaner code flow
  - Easier debugging

  5.2 Replace std::bind with Lambdas

  Low Effort, Medium Impact:

  // Before
  channel->start(std::bind(&channel::do_start, shared_from_base<channel>(), _1, handler));

  // After
  channel->start([self = shared_from_base<channel>(), handler](code const& ec) {
      self->do_start(ec, handler);
  });

  5.3 Use asio::cancellation_slot (Asio 1.21+)

  For cleaner timeout/cancellation handling instead of manual timer races:

  asio::cancellation_signal signal;
  auto slot = signal.slot();
  async_connect(socket, endpoints, asio::bind_cancellation_slot(slot, handler));
  // Later: signal.emit(asio::cancellation_type::total);

  5.4 Consider asio::deferred (Asio 1.22+)

  For composing async operations without immediate execution.

  ---
  6. Parallelization Opportunities

  6.1 Current Parallelism

  - Good: Multiple connections handled concurrently via io_context::run() on multiple threads
  - Good: concurrent() dispatch for truly parallel operations
  - Good: Read operations on different channels are parallel

  6.2 Potential Improvements

  1. Message serialization: Currently synchronous in proxy::send(). Could be moved to a worker pool.
  2. Batch broadcast: The p2p::broadcast() function could use asio::parallel_group (C++14/Asio 1.22+):
  template <typename Message>
  void broadcast(Message const& message, channel_handler handle_channel, result_handler handle_complete) {
      auto const channels = pending_close_.collection();
      // Could use parallel_group for better coordination
  }

  3. DNS resolution: Multiple async_resolve calls could run in parallel for seed nodes.

  ---
  7. Specific Issues to Address

  | Priority | File                         | Issue                                           |
  |----------|------------------------------|-------------------------------------------------|
  | Medium   | work.hpp:65-66               | TODO: Validate bind_executor vs wrap() behavior |
  | Low      | hosts.cpp:20                 | TODO: Change to bimap hash table                |
  | Low      | session.hpp:146              | TODO: Create session_timer base class           |
  | Low      | protocol_reject_70002.cpp:39 | TODO: Mitigate log fill DOS                     |

  ---
  8. Recommendations Summary

  Immediate (No Breaking Changes):

  1. ✅ Resolve Issue #76 (bind_executor vs wrap())
  2. ✅ Replace std::bind with lambdas incrementally
  3. ✅ Remove commented-out code blocks
  4. ✅ Replace binding macros with templates/lambdas

  Medium Term (Incremental):

  1. Add asio::cancellation_slot support for cleaner cancellation
  2. Consider moving to asio::deferred for composable operations
  3. Implement the session_timer base class (TODO noted in code)

  Long Term (Coroutines Migration):

  1. Start with new protocols using asio::awaitable
  2. Create coroutine wrappers for existing APIs
  3. Gradually migrate critical paths (connection, handshake)
  4. Full migration once C++20 is baseline requirement

  ---
  9. Conclusion

  The architecture is fundamentally sound. The codebase uses correct Asio patterns and has proper thread safety. The main technical
  debt is the callback-based design which, while correct, is harder to maintain than modern coroutine-based approaches.

  Migration to coroutines is highly recommended for long-term maintainability and would significantly simplify the code. The current
   design does not have blocking architectural issues that would prevent this migration.

  Parallelization is already good for a P2P networking stack. The main bottleneck would be at the application layer (blockchain
  processing), not the networking layer.

⏺ La revisión completa está terminada. El reporte anterior cubre todos los aspectos que pediste:

  Resumen ejecutivo:

  1. Arquitectura fundamentalmente sólida - No hay fallas arquitectónicas críticas. Los patrones de Asio están correctamente
  implementados.
  2. Concurrencia bien manejada - Uso correcto de:
    - atomic para flags
    - upgrade_mutex para colecciones
    - strand para serialización
    - sequencer para operaciones secuenciales async
  3. No hay race conditions críticas - Algunas son benignas y están documentadas en comentarios.
  4. Oportunidades de modernización:
    - Coroutines (C++20): El mayor beneficio sería migrar de callbacks a asio::awaitable. Esto simplificaría enormemente el código.
    - Reemplazar std::bind con lambdas: Más legible y menos propenso a errores
    - Usar asio::cancellation_slot: Para manejo más limpio de timeouts
  5. Paralelización ya es buena - El cuello de botella probablemente esté en blockchain, no en networking.
  6. Issue pendiente: El TODO en work.hpp:65-66 sobre bind_executor vs wrap() debería ser resuelto.

  ¿Querés que profundice en algún aspecto específico o que empecemos a planificar la migración a coroutines?


------


--------------------------------------------------------------------------------------------------------------------

Ahora, yo lo que me temo es que como este es codigo viejo (hace poco hicimos el cambio de io_service a io_context pero el resto
quedo igual)
Se implementan muchas cosas manualmente

Channels, Sessions, timers, threadpool, ...

O sea... sería bueno saber cómo se haría todo esto de cero.
O sea, tiene un nodo que se conecta a la red P2P, si tuvieras que escribir todo el networking de 0 en C++23, cómo lo harías?

⏺ Excelente pregunta. Voy a analizar cómo se haría el networking de cero con C++23 y Asio moderno, comparándolo con lo que tienen
  actualmente.

  Networking P2P desde Cero con C++23

  1. Lo que ya no necesitarías implementar manualmente

  | Componente Actual           | Reemplazo Moderno                                             |
  |-----------------------------|---------------------------------------------------------------|
  | threadpool + work_guard     | asio::thread_pool (built-in)                                  |
  | sequencer                   | asio::strand + coroutines                                     |
  | dispatcher (4 métodos)      | co_spawn + strand                                             |
  | synchronizer                | asio::experimental::parallel_group o when_all                 |
  | pending<T> collection       | asio::experimental::channel o simplemente std::vector + mutex |
  | deadline timer wrapper      | asio::steady_timer + co_await directo                         |
  | subscriber/resubscriber     | asio::experimental::channel<void(error_code, T)>              |
  | Callback chains (std::bind) | Coroutines lineales                                           |
  | enable_shared_from_base     | Sigue necesario, pero menos crítico con coroutines            |

  2. Arquitectura Moderna Propuesta

  ┌─────────────────────────────────────────────────────────────┐
  │                        p2p_node                             │
  │  - asio::thread_pool                                        │
  │  - asio::experimental::channel<peer_event>                  │
  ├─────────────────────────────────────────────────────────────┤
  │                     peer_manager                            │
  │  - std::unordered_map<peer_id, peer_session>               │
  │  - asio::strand (para mutación del map)                    │
  ├─────────────────────────────────────────────────────────────┤
  │                     peer_session                            │
  │  - asio::ip::tcp::socket                                   │
  │  - asio::steady_timer (inactivity)                         │
  │  - asio::experimental::channel<message> (outbound queue)   │
  │  - Coroutine: read_loop(), write_loop()                    │
  ├─────────────────────────────────────────────────────────────┤
  │                      protocols                              │
  │  - Coroutines que consumen/producen messages               │
  └─────────────────────────────────────────────────────────────┘

  3. Código de Ejemplo - Implementación Moderna

  // ============================================================================
  // p2p_node.hpp - El nodo completo
  // ============================================================================
  #pragma once

  #include <asio.hpp>
  #include <asio/experimental/channel.hpp>
  #include <asio/experimental/parallel_group.hpp>
  #include <expected>
  #include <memory>
  #include <unordered_map>

  namespace kth::network {

  using asio::awaitable;
  using asio::co_spawn;
  using asio::detached;
  using asio::use_awaitable;

  // ----------------------------------------------------------------------------
  // Tipos básicos
  // ----------------------------------------------------------------------------
  using error = std::error_code;
  using peer_id = uint64_t;

  struct peer_event {
      enum class type { connected, disconnected, message_received };
      type event_type;
      peer_id id;
      std::vector<std::byte> data;
  };

  // ----------------------------------------------------------------------------
  // peer_session - Una conexión a un peer
  // ----------------------------------------------------------------------------
  class peer_session : public std::enable_shared_from_this<peer_session> {
  public:
      using ptr = std::shared_ptr<peer_session>;
      using message_channel = asio::experimental::channel<void(error, std::vector<std::byte>)>;

      peer_session(asio::ip::tcp::socket socket, peer_id id)
          : socket_(std::move(socket))
          , strand_(socket_.get_executor())
          , id_(id)
          , outbound_(socket_.get_executor(), 64)  // buffer 64 messages
          , inactivity_timer_(socket_.get_executor())
      {}

      // Inicia las coroutines de lectura/escritura
      awaitable<void> run() {
          using namespace asio::experimental::awaitable_operators;

          // Correr read_loop y write_loop en paralelo
          // Si cualquiera termina (error o cierre), cancelar la otra
          co_await (read_loop() && write_loop() && inactivity_loop());
      }

      // Enviar mensaje (thread-safe, non-blocking)
      awaitable<void> send(std::vector<std::byte> message) {
          co_await outbound_.async_send(error{}, std::move(message), use_awaitable);
      }

      void close() {
          asio::post(strand_, [self = shared_from_this()] {
              self->socket_.close();
              self->outbound_.close();
          });
      }

      peer_id id() const { return id_; }

  private:
      awaitable<void> read_loop() {
          std::array<std::byte, 4096> buffer;

          while (socket_.is_open()) {
              // Leer header (24 bytes en Bitcoin)
              auto [ec, header_bytes] = co_await asio::async_read(
                  socket_,
                  asio::buffer(buffer.data(), 24),
                  asio::as_tuple(use_awaitable)
              );

              if (ec) co_return;

              // Parsear header, obtener payload size
              auto payload_size = parse_header(buffer);
              if (payload_size > max_message_size) co_return;

              // Leer payload
              std::vector<std::byte> payload(payload_size);
              auto [ec2, _] = co_await asio::async_read(
                  socket_,
                  asio::buffer(payload),
                  asio::as_tuple(use_awaitable)
              );

              if (ec2) co_return;

              // Reset inactivity timer
              reset_inactivity();

              // Procesar mensaje (dispatch a protocol handlers)
              co_await process_message(std::move(payload));
          }
      }

      awaitable<void> write_loop() {
          while (socket_.is_open()) {
              // Esperar mensaje en la cola
              auto [ec, message] = co_await outbound_.async_receive(asio::as_tuple(use_awaitable));

              if (ec) co_return;

              // Escribir mensaje completo
              auto [write_ec, _] = co_await asio::async_write(
                  socket_,
                  asio::buffer(message),
                  asio::as_tuple(use_awaitable)
              );

              if (write_ec) co_return;
          }
      }

      awaitable<void> inactivity_loop() {
          while (socket_.is_open()) {
              inactivity_timer_.expires_after(std::chrono::minutes(10));

              auto [ec] = co_await inactivity_timer_.async_wait(asio::as_tuple(use_awaitable));

              if (ec == asio::error::operation_aborted) {
                  continue;  // Timer fue reseteado
              }

              // Timeout - cerrar conexión
              socket_.close();
              co_return;
          }
      }

      void reset_inactivity() {
          inactivity_timer_.cancel();
      }

      awaitable<void> process_message(std::vector<std::byte> payload);
      size_t parse_header(std::span<std::byte const> header);

      static constexpr size_t max_message_size = 32 * 1024 * 1024;

      asio::ip::tcp::socket socket_;
      asio::strand<asio::any_io_executor> strand_;
      peer_id id_;
      message_channel outbound_;
      asio::steady_timer inactivity_timer_;
  };

  // ----------------------------------------------------------------------------
  // peer_manager - Gestiona todas las conexiones
  // ----------------------------------------------------------------------------
  class peer_manager {
  public:
      peer_manager(asio::any_io_executor exec)
          : exec_(exec)
          , strand_(exec)
      {}

      awaitable<void> add_peer(asio::ip::tcp::socket socket) {
          auto id = next_id_++;
          auto session = std::make_shared<peer_session>(std::move(socket), id);

          // Agregar al map (en el strand)
          co_await asio::post(strand_, use_awaitable);
          peers_[id] = session;

          // Iniciar la sesión
          co_spawn(exec_, session->run(), [this, id](std::exception_ptr) {
              // Cuando termina, remover del map
              asio::post(strand_, [this, id] { peers_.erase(id); });
          });
      }

      // Broadcast a todos los peers
      awaitable<void> broadcast(std::vector<std::byte> message) {
          // Copiar lista de peers (snapshot)
          co_await asio::post(strand_, use_awaitable);
          auto peers_copy = peers_;

          // Enviar a todos en paralelo
          for (auto& [id, session] : peers_copy) {
              co_spawn(exec_, session->send(message), detached);
          }
      }

      size_t connection_count() const {
          return peers_.size();
      }

  private:
      asio::any_io_executor exec_;
      asio::strand<asio::any_io_executor> strand_;
      std::unordered_map<peer_id, peer_session::ptr> peers_;
      std::atomic<peer_id> next_id_{0};
  };

  // ----------------------------------------------------------------------------
  // connector - Conexiones salientes
  // ----------------------------------------------------------------------------
  class connector {
  public:
      connector(asio::any_io_executor exec, peer_manager& manager)
          : exec_(exec)
          , resolver_(exec)
          , manager_(manager)
      {}

      awaitable<std::expected<void, error>> connect(
          std::string host,
          uint16_t port,
          std::chrono::seconds timeout = std::chrono::seconds{30}
      ) {
          using namespace asio::experimental::awaitable_operators;

          // Resolver DNS
          auto [resolve_ec, endpoints] = co_await resolver_.async_resolve(
              host, std::to_string(port),
              asio::as_tuple(use_awaitable)
          );

          if (resolve_ec) {
              co_return std::unexpected(resolve_ec);
          }

          // Crear socket y timer
          asio::ip::tcp::socket socket(exec_);
          asio::steady_timer timer(exec_);
          timer.expires_after(timeout);

          // Race: connect vs timeout
          auto result = co_await (
              asio::async_connect(socket, endpoints, asio::as_tuple(use_awaitable)) ||
              timer.async_wait(asio::as_tuple(use_awaitable))
          );

          if (result.index() == 1) {
              // Timeout ganó
              co_return std::unexpected(asio::error::timed_out);
          }

          auto [connect_ec, endpoint] = std::get<0>(result);
          if (connect_ec) {
              co_return std::unexpected(connect_ec);
          }

          // Éxito - agregar al manager
          co_await manager_.add_peer(std::move(socket));
          co_return {};
      }

  private:
      asio::any_io_executor exec_;
      asio::ip::tcp::resolver resolver_;
      peer_manager& manager_;
  };

  // ----------------------------------------------------------------------------
  // acceptor - Conexiones entrantes
  // ----------------------------------------------------------------------------
  class acceptor {
  public:
      acceptor(asio::any_io_executor exec, peer_manager& manager, uint16_t port)
          : acceptor_(exec, {asio::ip::tcp::v6(), port})
          , manager_(manager)
      {
          acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
      }

      awaitable<void> run() {
          while (acceptor_.is_open()) {
              auto [ec, socket] = co_await acceptor_.async_accept(asio::as_tuple(use_awaitable));

              if (ec) {
                  if (ec == asio::error::operation_aborted) co_return;
                  continue;  // Retry on other errors
              }

              co_await manager_.add_peer(std::move(socket));
          }
      }

      void stop() {
          acceptor_.close();
      }

  private:
      asio::ip::tcp::acceptor acceptor_;
      peer_manager& manager_;
  };

  // ----------------------------------------------------------------------------
  // p2p_node - El nodo completo
  // ----------------------------------------------------------------------------
  class p2p_node {
  public:
      p2p_node(size_t thread_count = std::thread::hardware_concurrency())
          : pool_(thread_count)
          , manager_(pool_.get_executor())
          , connector_(pool_.get_executor(), manager_)
      {}

      void start(uint16_t listen_port) {
          acceptor_ = std::make_unique<acceptor>(pool_.get_executor(), manager_, listen_port);
          co_spawn(pool_, acceptor_->run(), detached);
      }

      awaitable<void> connect_to(std::string host, uint16_t port) {
          auto result = co_await connector_.connect(host, port);
          if (!result) {
              // Log error: result.error()
          }
      }

      awaitable<void> broadcast(std::vector<std::byte> message) {
          co_await manager_.broadcast(std::move(message));
      }

      void stop() {
          if (acceptor_) acceptor_->stop();
          pool_.stop();
      }

      void join() {
          pool_.join();
      }

      size_t connection_count() const {
          return manager_.connection_count();
      }

  private:
      asio::thread_pool pool_;
      peer_manager manager_;
      connector connector_;
      std::unique_ptr<acceptor> acceptor_;
  };

  }  // namespace kth::network

  4. Comparación de Complejidad

  | Aspecto                     | Código Actual                                                              | Código Moderno
                                           |
  |-----------------------------|----------------------------------------------------------------------------|----------------------
  -----------------------------------------|
  | Líneas de código (estimado) | ~5000                                                                      | ~800
                                           |
  | Clases auxiliares           | 15+ (sequencer, dispatcher, work, pending, synchronizer, subscriber, etc.) | 5 (peer_session,
  peer_manager, connector, acceptor, p2p_node) |
  | Callback depth              | 4-6 niveles                                                                | 0 (código lineal)
                                           |
  | Thread safety               | Manual (mutexes everywhere)                                                | Automática (strands +
   channels)                               |
  | Error handling              | Error codes pasados en callbacks                                           | std::expected +
  excepciones naturales                         |
  | Cancellation                | Manual con stopped_ flags                                                  | Automática con
  cancel()                                       |

  5. Qué Desaparece Completamente

  1. threadpool.hpp/cpp → asio::thread_pool (2 líneas)
  2. work.hpp → No necesario, co_spawn maneja lifetime
  3. sequencer.hpp/cpp → strand + coroutines naturalmente secuenciales
  4. dispatcher.hpp → co_spawn + strand
  5. synchronizer.hpp → parallel_group o awaitable_operators
  6. pending.hpp/ipp → std::unordered_map simple
  7. subscriber.hpp/ipp → asio::experimental::channel
  8. resubscriber.hpp/ipp → asio::experimental::channel
  9. deadline.hpp/cpp → asio::steady_timer directo
  10. Todos los BIND macros → Lambdas o nada (código lineal)
  11. delegates.hpp → No necesario

  6. Protocol Handlers con Coroutines

  // Antes (callback hell):
  void protocol_version_31402::start(result_handler handler) {
      send<CLASS>(version_message, &CLASS::handle_send_version, handler);
  }

  void protocol_version_31402::handle_send_version(code const& ec, result_handler handler) {
      if (ec) { handler(ec); return; }
      subscribe<CLASS, version>(&CLASS::handle_receive_version, handler);
  }

  void protocol_version_31402::handle_receive_version(code const& ec, version_const_ptr msg, result_handler handler) {
      // ... más callbacks
  }

  // Después (lineal):
  awaitable<std::expected<version_info, error>> handshake_protocol(peer_session& peer) {
      // 1. Enviar version
      auto send_result = co_await peer.send(make_version_message());
      if (!send_result) co_return std::unexpected(send_result.error());

      // 2. Recibir version
      auto version_msg = co_await peer.receive<version_message>();
      if (!version_msg) co_return std::unexpected(version_msg.error());

      // 3. Enviar verack
      co_await peer.send(make_verack_message());

      // 4. Recibir verack
      auto verack = co_await peer.receive<verack_message>();
      if (!verack) co_return std::unexpected(verack.error());

      // 5. Retornar info del peer
      co_return version_info{
          .version = version_msg->version,
          .services = version_msg->services,
          .user_agent = version_msg->user_agent
      };
  }

  7. Recomendación de Migración

  Fase 1: Infraestructura (sin romper nada)
  ├── Agregar asio::thread_pool como alternativa
  ├── Crear wrappers awaitable para operaciones existentes
  └── Escribir nuevos protocolos con coroutines

  Fase 2: Peer Session
  ├── Reescribir peer_session con coroutines
  ├── Mantener API externa compatible
  └── Eliminar sequencer interno

  Fase 3: Session Management
  ├── Simplificar session_inbound/outbound
  ├── Eliminar subscriber/resubscriber
  └── Usar channels para eventos

  Fase 4: Cleanup
  ├── Eliminar clases auxiliares no usadas
  ├── Simplificar p2p a la nueva arquitectura
  └── Eliminar macros BIND

  ---
  En resumen: Con C++23 y Asio moderno, aproximadamente el 70-80% del código de infraestructura actual es innecesario. Las
  coroutines eliminan la necesidad de casi todas las clases auxiliares de sincronización y dispatch.

  ¿Querés que empiece a diseñar un plan concreto de migración fase por fase?


---

# PLAN DE MIGRACIÓN A ASIO MODERNO CON COROUTINES

## Resumen Ejecutivo

**Objetivo**: Migrar de la arquitectura callback-based actual a coroutines C++23 con Asio moderno.

**Alcance**:
- ~3,783 líneas en infrastructure/utility (objetivo: reducir 70%)
- ~6,450 líneas en network (objetivo: reducir 50%)
- Eliminar ~11 clases auxiliares innecesarias

**Estrategia**: Migración incremental sin breaking changes, manteniendo backwards compatibility hasta fase final.

---

## Dependencias Identificadas

```
synchronizer.hpp ← blockchain/populate, blockchain/validate, dispatcher.hpp
dispatcher.hpp   ← subscriber.hpp, resubscriber.hpp, database/data_base.hpp
work.hpp         ← dispatcher.hpp, delegates.hpp
sequencer.hpp    ← work.hpp
subscriber.hpp   ← network/message_subscriber
resubscriber.hpp ← blockchain/block_organizer, blockchain/transaction_organizer
pending.hpp      ← network/p2p (pending_connect, pending_handshake, pending_close)
```

**Orden de migración recomendado** (de menos a más dependencias):
1. sequencer → 2. work → 3. synchronizer → 4. dispatcher → 5. subscriber/resubscriber → 6. pending

---

## Clases Legacy a Eliminar/Modernizar

Estas clases fueron escritas ~2011-2016 y ya no son necesarias con Asio moderno + coroutines:

| Clase | Ubicación | Problema | Reemplazo Moderno | Fase |
|-------|-----------|----------|-------------------|------|
| `sequencer` | infrastructure/utility | Reimplementa `asio::strand` manualmente con mutex+queue | `asio::strand<>` nativo | Fase 3 |
| `work` | infrastructure/utility | Wrappers triviales sobre `asio::post()` | `co_await asio::post(executor, use_awaitable)` | Fase 3 |
| `deadline` | infrastructure/utility | Wrapper con mutex innecesario sobre `steady_timer` | `asio::steady_timer` + strand | Fase 2 |
| `socket` | infrastructure/utility | Mutex manual innecesario si se usa strand | Socket directo con strand | Fase 2 |
| `synchronizer` | infrastructure/utility | Contador con callbacks | `std::latch`/`std::barrier` o coroutine simple | Fase 3 |
| `dispatcher` | infrastructure/utility | Event dispatch con callbacks | `concurrent_channel` + coroutines | Fase 3 |
| `subscriber` | infrastructure/utility | Pub/sub con callbacks | `concurrent_channel` broadcast | Fase 4 |
| `resubscriber` | infrastructure/utility | subscriber + auto-reconnect | Channel + retry loop | Fase 4 |
| `pending` | network | Tracking de operaciones pendientes | Eliminado con coroutines (await implícito) | Fase 2 |
| `proxy` | network | Wrapper complejo de socket+channels | `peer_session` coroutine-based | Fase 2 |
| `channel` (network) | network | Message framing sobre socket | Integrado en `peer_session` | Fase 2 |

### Análisis Detallado

**`sequencer`** (sequencer.hpp/cpp):
```cpp
// ANTES (2011): Cola manual con mutex
class sequencer {
    void lock(action&& handler);  // encola
    void unlock();                // desencola y ejecuta siguiente
    std::queue<action> actions_;
    mutable shared_mutex mutex_;
};

// AHORA (2025): strand nativo
asio::strand<asio::any_io_executor> strand(executor);
co_await asio::post(strand, asio::use_awaitable);  // serializado automáticamente
```

**`work`** (work.hpp):
```cpp
// ANTES: Wrappers innecesarios
work.concurrent(handler, args...);  // → asio::post(executor_, bind(...))
work.ordered(handler, args...);     // → asio::post(strand_, bind(...))

// AHORA: Uso directo
co_await asio::post(executor, asio::use_awaitable);
co_await asio::post(strand, asio::use_awaitable);
```

**`deadline`** (deadline.cpp):
```cpp
// ANTES: Mutex manual para thread-safety
class deadline {
    void start(handler, duration);
    void stop();
    mutable upgrade_mutex mutex_;  // innecesario con strand
};

// AHORA: Timer directo en strand
asio::steady_timer timer(strand);
timer.expires_after(5s);
auto [ec] = co_await timer.async_wait(as_tuple(use_awaitable));
```

**`socket`** (socket.cpp):
```cpp
// ANTES: Mutex para acceso al socket
class socket {
    asio::socket& get();  // con lock
    void stop();          // con lock
    mutable shared_mutex mutex_;
};

// AHORA: Socket en strand, sin mutex
// El strand garantiza que solo un handler accede al socket a la vez
```

---

## FASE 0: Preparación (Pre-requisitos)

### 0.1 Verificar soporte de compiladores
```
Mínimos requeridos:
- GCC 12+ (coroutines estables)
- Clang 16+ (coroutines estables)
- MSVC 19.30+ (VS 2022 17.0+)
```

**Acción**: Actualizar CI/CD y documentar requisitos.

### 0.2 Actualizar Asio
```cpp
// Verificar que tenemos Asio 1.22+ o Boost 1.80+
// Necesario para:
// - asio::experimental::channel
// - asio::experimental::parallel_group
// - asio::awaitable_operators
```

**Acción**: Verificar versión actual y actualizar si es necesario.

### 0.3 Crear feature flag
```cpp
// kth/infrastructure/config.hpp
#if __cplusplus >= 202002L && defined(__cpp_impl_coroutine)
    #define KTH_USE_COROUTINES 1
#else
    #define KTH_USE_COROUTINES 0
#endif
```

**Acción**: Agregar macro para conditional compilation durante migración.

### 0.4 Tests de integración de coroutines
**Acción**: Crear un test mínimo que verifique que coroutines funcionan con la configuración actual.

```cpp
// test/coroutine_smoke_test.cpp
#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>

asio::awaitable<int> test_coro() {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::use_awaitable);
    co_return 42;
}

TEST(Coroutines, SmokeTest) {
    asio::io_context ctx;
    int result = 0;
    asio::co_spawn(ctx, test_coro(), [&](std::exception_ptr, int r) { result = r; });
    ctx.run();
    EXPECT_EQ(result, 42);
}
```

---

## FASE 1: Infraestructura Base (Sin romper nada)

### 1.1 Agregar asio::thread_pool como alternativa
**Archivo**: `src/infrastructure/include/kth/infrastructure/utility/thread_pool.hpp` (NUEVO)

```cpp
#pragma once

#include <asio.hpp>

namespace kth {

#if KTH_USE_COROUTINES

// Nueva implementación minimalista
class thread_pool_modern {
public:
    explicit thread_pool_modern(size_t threads = std::thread::hardware_concurrency())
        : pool_(threads) {}

    asio::any_io_executor get_executor() {
        return pool_.get_executor();
    }

    void stop() { pool_.stop(); }
    void join() { pool_.join(); }

private:
    asio::thread_pool pool_;
};

#endif

// Alias condicional
#if KTH_USE_COROUTINES
using thread_pool_t = thread_pool_modern;
#else
using thread_pool_t = threadpool;  // existing class
#endif

} // namespace kth
```

**Tests**: Verificar que thread_pool_modern funciona igual que threadpool existente.

### 1.2 Crear wrappers awaitable para operaciones existentes
**Archivo**: `src/infrastructure/include/kth/infrastructure/utility/awaitable_helpers.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth {

using asio::awaitable;
using asio::use_awaitable;

// Helper para convertir callbacks existentes a awaitables
template <typename T>
awaitable<std::expected<T, code>> to_awaitable(auto async_op) {
    // Implementación con asio::async_initiate
}

// Timeout wrapper
template <typename T>
awaitable<std::expected<T, code>> with_timeout(
    awaitable<T> op,
    std::chrono::milliseconds timeout
) {
    using namespace asio::experimental::awaitable_operators;
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor);
    timer.expires_after(timeout);

    auto result = co_await (
        std::move(op) ||
        timer.async_wait(use_awaitable)
    );

    if (result.index() == 1) {
        co_return std::unexpected(error::channel_timeout);
    }
    co_return std::get<0>(std::move(result));
}

} // namespace kth

#endif
```

### 1.3 Agregar channel para comunicación async
**Archivo**: `src/infrastructure/include/kth/infrastructure/utility/async_channel.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

#include <asio/experimental/channel.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace kth {

// =============================================================================
// IMPORTANTE: Thread Safety de Channels
// =============================================================================
//
// asio::experimental::channel:
//   - Thread Safety: "Distinct objects: Safe. Shared objects: Unsafe."
//   - Para uso en el MISMO strand/thread
//   - Más eficiente (sin locks internos)
//   - Usar cuando todas las coroutines corren en el mismo strand
//
// asio::experimental::concurrent_channel:
//   - Thread Safety: Safe para acceso concurrente desde múltiples threads
//   - Para uso desde DIFERENTES threads del pool
//   - Tiene locks internos, ligero overhead
//   - Usar cuando productores/consumidores corren en threads diferentes
//
// =============================================================================

// Channel para uso en el mismo strand (no thread-safe)
// Uso: Comunicación entre coroutines que corren en el mismo strand
// Ejemplo: Stages de procesamiento serializado, pipelines single-thread
template <typename... Args>
using async_channel = asio::experimental::channel<void(std::error_code, Args...)>;

// Channel thread-safe para uso desde múltiples threads
// Uso: Comunicación entre coroutines en diferentes threads del pool
// Ejemplo: Work queues, distribución de trabajo a múltiples workers
template <typename... Args>
using concurrent_channel = asio::experimental::concurrent_channel<void(std::error_code, Args...)>;

// Channel de eventos (sin datos, solo señalización)
using event_channel = async_channel<>;
using concurrent_event_channel = concurrent_channel<>;

} // namespace kth

#endif
```

### 1.4 Lecciones Aprendidas: Thread Safety de Channels

Durante la implementación y testing de channels, descubrimos un problema importante de concurrencia:

**El Problema**:
Un test con 5 productores concurrentes enviando valores al channel fallaba aleatoriamente.
Los valores se corrompían: esperábamos `sum=15` (1+2+3+4+5) pero obteníamos valores como 14, 11, 7.

```
FAIL: send_count=5 count=5 sum=14
  Received: 0 0 0 0 2   (esperado: 1 2 3 4 5 en cualquier orden)
```

**La Causa**:
`asio::experimental::channel` NO es thread-safe para acceso concurrente.
Documentación: "Distinct objects: Safe. Shared objects: Unsafe."
https://think-async.com/Asio/asio-1.36.0/doc/asio/reference/experimental__channel.html

**La Solución**:
- Usar `concurrent_channel` para escenarios multi-threaded (múltiples productores/consumidores en diferentes threads)
- O usar un `strand` para serializar acceso a un `channel` regular

**Guía de Uso en el Nodo**:

| Escenario | Tipo de Channel | Ejemplo |
|-----------|-----------------|---------|
| Comunicación intra-peer | `async_channel` + strand | read_loop → write_loop en peer_session |
| Pipeline serializado | `async_channel` + strand | Stages de validación de bloques |
| Work queue multi-worker | `concurrent_channel` | Distribución de transacciones a validadores |
| Múltiples peers → procesador | `concurrent_channel` | Mensajes de red hacia procesador central |

**Entregables Fase 1**:
- [x] `threadpool.hpp` con implementación usando `asio::thread_pool`
- [x] `awaitable_helpers.hpp` con utilities
- [x] `async_channel.hpp` con typedefs para `channel` y `concurrent_channel`
- [ ] Tests unitarios:
  - [x] threadpool: stop/join behavior
  - [ ] async_channel con strand (single-thread safe)
  - [ ] concurrent_channel sin strand (multi-thread safe)
- [ ] Build exitoso con standalone Asio 1.36.0

---

## FASE 2: Proxy/Channel Modernizado

### 2.1 Crear peer_session moderna (en paralelo a proxy/channel existente)
**Archivo**: `src/network/include/kth/network/peer_session.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

#include <asio.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>

namespace kth::network {

class peer_session : public std::enable_shared_from_this<peer_session> {
public:
    using ptr = std::shared_ptr<peer_session>;
    using message_channel = async_channel<std::vector<std::byte>>;

    peer_session(asio::ip::tcp::socket socket, settings const& settings);

    awaitable<void> run();
    awaitable<void> send(std::vector<std::byte> message);
    void close();

    // Compatibilidad con API existente
    infrastructure::config::authority authority() const;
    bool stopped() const;

private:
    awaitable<void> read_loop();
    awaitable<void> write_loop();
    awaitable<void> inactivity_loop();

    asio::ip::tcp::socket socket_;
    asio::strand<asio::any_io_executor> strand_;
    message_channel outbound_;
    asio::steady_timer inactivity_timer_;
    std::atomic<bool> stopped_{false};
    settings const& settings_;
};

} // namespace kth::network

#endif
```

### 2.2 Implementación de peer_session
**Archivo**: `src/network/src/peer_session.cpp` (NUEVO)

```cpp
#if KTH_USE_COROUTINES

#include <kth/network/peer_session.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

using namespace asio::experimental::awaitable_operators;

peer_session::peer_session(asio::ip::tcp::socket socket, settings const& settings)
    : socket_(std::move(socket))
    , strand_(socket_.get_executor())
    , outbound_(socket_.get_executor(), 64)
    , inactivity_timer_(socket_.get_executor())
    , settings_(settings)
{}

awaitable<void> peer_session::run() {
    // Las tres coroutines corren en paralelo
    // Si cualquiera termina, las otras se cancelan automáticamente
    co_await (read_loop() && write_loop() && inactivity_loop());

    // Cleanup
    stopped_ = true;
    socket_.close();
    outbound_.close();
}

awaitable<void> peer_session::read_loop() {
    std::array<std::byte, 24> header_buffer;

    while (!stopped_) {
        // Leer header
        auto [ec, n] = co_await asio::async_read(
            socket_,
            asio::buffer(header_buffer),
            asio::as_tuple(use_awaitable)
        );

        if (ec) co_return;

        // Parse header para obtener payload size
        auto const payload_size = parse_message_header(header_buffer);
        if (payload_size > max_message_size) co_return;

        // Leer payload
        std::vector<std::byte> payload(payload_size);
        auto [ec2, _] = co_await asio::async_read(
            socket_,
            asio::buffer(payload),
            asio::as_tuple(use_awaitable)
        );

        if (ec2) co_return;

        // Reset inactivity timer
        inactivity_timer_.cancel();

        // Procesar mensaje
        co_await handle_message(header_buffer, std::move(payload));
    }
}

awaitable<void> peer_session::write_loop() {
    while (!stopped_) {
        auto [ec, message] = co_await outbound_.async_receive(
            asio::as_tuple(use_awaitable)
        );

        if (ec) co_return;

        auto [write_ec, _] = co_await asio::async_write(
            socket_,
            asio::buffer(message),
            asio::as_tuple(use_awaitable)
        );

        if (write_ec) co_return;
    }
}

awaitable<void> peer_session::inactivity_loop() {
    while (!stopped_) {
        inactivity_timer_.expires_after(settings_.channel_inactivity());

        auto [ec] = co_await inactivity_timer_.async_wait(
            asio::as_tuple(use_awaitable)
        );

        if (ec == asio::error::operation_aborted) {
            continue;  // Timer was reset, continue waiting
        }

        // Inactivity timeout
        co_return;
    }
}

awaitable<void> peer_session::send(std::vector<std::byte> message) {
    co_await outbound_.async_send(std::error_code{}, std::move(message), use_awaitable);
}

void peer_session::close() {
    asio::post(strand_, [self = shared_from_this()] {
        self->stopped_ = true;
        self->socket_.close();
        self->outbound_.close();
        self->inactivity_timer_.cancel();
    });
}

} // namespace kth::network

#endif
```

### 2.3 Adapter para usar peer_session donde se usaba channel
**Archivo**: `src/network/include/kth/network/channel_adapter.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

// Adapter que expone API compatible con channel existente
// pero usa peer_session internamente
class channel_adapter {
public:
    using ptr = std::shared_ptr<channel_adapter>;

    channel_adapter(peer_session::ptr session);

    // API compatible con channel existente
    template <typename Message>
    void send(Message const& message, result_handler handler) {
        auto data = serialize(message);
        asio::co_spawn(session_->get_executor(),
            session_->send(std::move(data)),
            [handler](std::exception_ptr ep) {
                handler(ep ? error::operation_failed : error::success);
            });
    }

    void stop(code const& ec);
    infrastructure::config::authority authority() const;
    // ... otros métodos de compatibilidad

private:
    peer_session::ptr session_;
};

} // namespace kth::network

#endif
```

**Entregables Fase 2**:
- [ ] `peer_session.hpp/cpp` implementado y testeado
- [ ] `channel_adapter.hpp` para compatibilidad
- [ ] Tests que comparan comportamiento peer_session vs channel existente
- [ ] Sin cambios a código existente todavía

---

## FASE 3: Connector/Acceptor Modernizado

### 3.1 Modern connector
**Archivo**: `src/network/include/kth/network/connector_coro.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

class connector_coro {
public:
    connector_coro(asio::any_io_executor exec, settings const& settings);

    awaitable<std::expected<peer_session::ptr, code>> connect(
        std::string const& host,
        uint16_t port
    );

    void stop();

private:
    asio::any_io_executor exec_;
    asio::ip::tcp::resolver resolver_;
    settings const& settings_;
    std::atomic<bool> stopped_{false};
};

} // namespace kth::network

#endif
```

### 3.2 Modern acceptor
**Archivo**: `src/network/include/kth/network/acceptor_coro.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

class acceptor_coro {
public:
    acceptor_coro(asio::any_io_executor exec, settings const& settings);

    awaitable<code> listen(uint16_t port);
    awaitable<std::expected<peer_session::ptr, code>> accept();
    void stop();

private:
    asio::ip::tcp::acceptor acceptor_;
    settings const& settings_;
};

} // namespace kth::network

#endif
```

**Entregables Fase 3**:
- [ ] `connector_coro.hpp/cpp` implementado
- [ ] `acceptor_coro.hpp/cpp` implementado
- [ ] Tests de conexión/aceptación

---

## FASE 4: Session Management Modernizado

### 4.1 Peer manager (reemplaza session + pending collections)
**Archivo**: `src/network/include/kth/network/peer_manager.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

class peer_manager {
public:
    peer_manager(asio::any_io_executor exec);

    awaitable<code> add_peer(peer_session::ptr session);
    void remove_peer(peer_id id);

    size_t connection_count() const;
    bool connected(infrastructure::config::authority const& addr) const;

    // Broadcast
    awaitable<void> broadcast(std::vector<std::byte> message);

    // Iteration (thread-safe snapshot)
    std::vector<peer_session::ptr> get_peers() const;

private:
    asio::any_io_executor exec_;
    asio::strand<asio::any_io_executor> strand_;
    std::unordered_map<peer_id, peer_session::ptr> peers_;
    std::atomic<peer_id> next_id_{0};
    mutable std::shared_mutex mutex_;
};

} // namespace kth::network

#endif
```

**Entregables Fase 4**:
- [x] `peer_manager.hpp/cpp` implementado (PR #144)
- [x] Elimina necesidad de `pending.hpp` para connections
- [x] Tests de gestión de peers

---

## FASE 5: Protocols con Coroutines

### Clases de Protocol Existentes

Las siguientes clases implementan el protocolo Bitcoin sobre TCP. Actualmente usan callbacks y macros `BIND`. Serán migradas a coroutines en esta fase.

| Clase | Archivo | Descripción | Dependencias | Complejidad |
|-------|---------|-------------|--------------|-------------|
| `protocol` | `protocols/protocol.hpp/cpp` | **Clase base** para todos los protocolos. Provee acceso a channel, timer, authority, versión, y métodos `send()` con callbacks. | `channel`, `timer` | Media |
| `protocol_events` | `protocols/protocol_events.hpp/cpp` | Base con eventos de start/stop. Extiende `protocol`. | `protocol` | Baja |
| `protocol_timer` | `protocols/protocol_timer.hpp/cpp` | Base con timer periódico. Extiende `protocol_events`. | `protocol_events`, `deadline` | Baja |
| `protocol_version_31402` | `protocols/protocol_version_31402.hpp/cpp` | **Handshake v1** (protocol version 31402). Envía/recibe `version` y `verack`. | `protocol_timer` | Alta |
| `protocol_version_70002` | `protocols/protocol_version_70002.hpp/cpp` | **Handshake v2** (protocol version 70002+). Añade relay flag y reject. | `protocol_version_31402` | Alta |
| `protocol_ping_31402` | `protocols/protocol_ping_31402.hpp/cpp` | **Ping básico** sin nonce. Responde a `ping` con `pong`. | `protocol_timer` | Baja |
| `protocol_ping_60001` | `protocols/protocol_ping_60001.hpp/cpp` | **Ping con nonce** (BIP 31). Mide latencia con nonce round-trip. | `protocol_ping_31402` | Media |
| `protocol_address_31402` | `protocols/protocol_address_31402.hpp/cpp` | **Intercambio de direcciones**. Envía/recibe `addr` y `getaddr`. | `protocol_timer` | Media |
| `protocol_seed_31402` | `protocols/protocol_seed_31402.hpp/cpp` | **Seeding inicial**. Obtiene direcciones de seed nodes. | `protocol_timer` | Media |
| `protocol_reject_70002` | `protocols/protocol_reject_70002.hpp/cpp` | **Manejo de reject** (BIP 61). Procesa mensajes de rechazo. | `protocol_events` | Baja |

### Jerarquía de Herencia

```
protocol (base)
├── protocol_events
│   ├── protocol_timer
│   │   ├── protocol_version_31402
│   │   │   └── protocol_version_70002
│   │   ├── protocol_ping_31402
│   │   │   └── protocol_ping_60001
│   │   ├── protocol_address_31402
│   │   └── protocol_seed_31402
│   └── protocol_reject_70002
```

### Estrategia de Migración de Protocols

**Orden recomendado** (de menos a más dependencias):

1. `protocol_coro` (nueva base con coroutines)
2. `protocol_events_coro` (eventos como channels)
3. `protocol_timer_coro` (timer con co_await)
4. `protocol_reject_coro` (simple, sin estado complejo)
5. `protocol_ping_31402_coro` y `protocol_ping_60001_coro`
6. `protocol_address_31402_coro`
7. `protocol_seed_31402_coro`
8. `protocol_version_31402_coro` y `protocol_version_70002_coro` (críticos para handshake)

### 5.1 Protocol base con coroutines
**Archivo**: `src/network/include/kth/network/protocols/protocol_coro.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

// Base para protocolos con coroutines
class protocol_coro {
public:
    protocol_coro(peer_session::ptr session);

    virtual awaitable<code> run() = 0;

protected:
    // Helpers para enviar/recibir mensajes tipados
    template <typename Message>
    awaitable<code> send(Message const& msg);

    template <typename Message>
    awaitable<std::expected<Message, code>> receive();

    template <typename Message>
    awaitable<std::expected<Message, code>> receive_with_timeout(
        std::chrono::milliseconds timeout
    );

    peer_session::ptr session_;
};

} // namespace kth::network

#endif
```

### 5.2 Handshake protocol moderno
**Archivo**: `src/network/include/kth/network/protocols/protocol_version_coro.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

struct handshake_result {
    uint32_t version;
    uint64_t services;
    std::string user_agent;
};

class protocol_version_coro : public protocol_coro {
public:
    protocol_version_coro(peer_session::ptr session, settings const& settings);

    awaitable<std::expected<handshake_result, code>> run() override;

private:
    awaitable<code> send_version();
    awaitable<std::expected<domain::message::version, code>> receive_version();
    awaitable<code> send_verack();
    awaitable<code> receive_verack();

    settings const& settings_;
};

// Implementación lineal y legible:
awaitable<std::expected<handshake_result, code>> protocol_version_coro::run() {
    // 1. Enviar version
    if (auto ec = co_await send_version(); ec) {
        co_return std::unexpected(ec);
    }

    // 2. Recibir version
    auto version_result = co_await receive_version();
    if (!version_result) {
        co_return std::unexpected(version_result.error());
    }

    // 3. Validar version
    if (version_result->value < minimum_version) {
        co_return std::unexpected(error::accept_failed);
    }

    // 4. Enviar verack
    if (auto ec = co_await send_verack(); ec) {
        co_return std::unexpected(ec);
    }

    // 5. Recibir verack (con timeout)
    auto verack = co_await receive_with_timeout<verack_message>(
        std::chrono::seconds{30}
    );
    if (!verack) {
        co_return std::unexpected(verack.error());
    }

    // 6. Éxito
    co_return handshake_result{
        .version = version_result->value,
        .services = version_result->services,
        .user_agent = version_result->user_agent
    };
}

} // namespace kth::network

#endif
```

**Entregables Fase 5**:
- [x] `protocols_coro.hpp/cpp` - funciones libres con coroutines (PR #145)
  - `wait_for_message<T>()` y `wait_for_any_message()` helpers
  - `perform_handshake()` para version/verack exchange
  - `run_ping_pong()` para keepalive loop
  - `request_addresses()` y `send_addresses()` para addr protocol
- [x] Tests para cada protocolo

---

## FASE 6: P2P Node Modernizado

### 6.1 p2p_node moderno
**Archivo**: `src/network/include/kth/network/p2p_node.hpp` (NUEVO)

```cpp
#pragma once

#if KTH_USE_COROUTINES

namespace kth::network {

class p2p_node {
public:
    p2p_node(settings const& settings);
    ~p2p_node();

    // Lifecycle
    awaitable<code> start();
    void stop();
    void join();

    // Connections
    awaitable<code> connect(std::string const& host, uint16_t port);

    // Broadcast
    template <typename Message>
    awaitable<void> broadcast(Message const& msg);

    // Properties
    size_t connection_count() const;
    bool stopped() const;
    settings const& network_settings() const;

    // Subscriptions (usando channels)
    async_channel<peer_session::ptr>& connection_channel();
    event_channel& stop_channel();

private:
    awaitable<void> run_acceptor();
    awaitable<void> run_outbound_sessions();
    awaitable<void> run_seeding();

    settings const& settings_;
    asio::thread_pool pool_;
    peer_manager manager_;
    connector_coro connector_;
    std::unique_ptr<acceptor_coro> acceptor_;

    async_channel<peer_session::ptr> connection_events_;
    event_channel stop_events_;

    std::atomic<bool> stopped_{true};
};

} // namespace kth::network

#endif
```

### 6.2 TODOs pendientes de p2p_node

**`get_peers()` sync method**: El método `std::vector<peer_session::ptr> get_peers() const` actualmente retorna un vector vacío. Opciones:
1. **Eliminar el método**: Forzar a los usuarios a usar `peers().all()` (async)
2. **Implementar con run_sync**: Usar un helper para ejecutar el coroutine sincrónicamente (no recomendado, puede bloquear)
3. **Cache interno**: Mantener un snapshot actualizado en cada add/remove (overhead)

**Recomendación**: Eliminar `get_peers()` sync y solo exponer `peers()` que da acceso a `peer_manager` con su método async `all()`.

**Entregables Fase 6**:
- [x] `p2p_node.hpp/cpp` implementado
- [ ] Resolver `get_peers()` sync (eliminar o implementar)
- [ ] Funcionalidad equivalente a `p2p` existente
- [ ] Tests unitarios de p2p_node

---

## FASE 7: Cleanup (Post-migración)

### 7.1 Eliminar código legacy
Una vez que la migración está completa y validada en producción:

```bash
# Archivos a eliminar:
src/infrastructure/include/kth/infrastructure/utility/sequencer.hpp
src/infrastructure/src/utility/sequencer.cpp
src/infrastructure/include/kth/infrastructure/utility/work.hpp
src/infrastructure/src/utility/work.cpp
src/infrastructure/include/kth/infrastructure/utility/dispatcher.hpp
src/infrastructure/src/utility/dispatcher.cpp
src/infrastructure/include/kth/infrastructure/utility/synchronizer.hpp
src/infrastructure/include/kth/infrastructure/utility/subscriber.hpp
src/infrastructure/include/kth/infrastructure/impl/utility/subscriber.ipp
src/infrastructure/include/kth/infrastructure/utility/resubscriber.hpp
src/infrastructure/include/kth/infrastructure/impl/utility/resubscriber.ipp
src/infrastructure/include/kth/infrastructure/utility/delegates.hpp

# Network legacy:
src/network/include/kth/network/proxy.hpp
src/network/src/proxy.cpp
src/network/include/kth/network/channel.hpp
src/network/src/channel.cpp
# ... etc
```

### 7.2 Renombrar p2p_node a p2p
Una vez que `p2p.hpp` y `p2p.cpp` legacy sean eliminados:
```bash
# Renombrar archivos:
mv src/network/include/kth/network/p2p_node.hpp src/network/include/kth/network/p2p.hpp
mv src/network/src/p2p_node.cpp src/network/src/p2p.cpp

# Renombrar clase:
# p2p_node → p2p
# connection_result puede quedarse igual o moverse a su propio header
```

### 7.3 Actualizar includes en infrastructure.hpp
```cpp
// Remover:
// #include <kth/infrastructure/utility/dispatcher.hpp>
// #include <kth/infrastructure/utility/sequencer.hpp>
// ... etc

// Mantener:
#include <kth/infrastructure/utility/thread_pool.hpp>
#include <kth/infrastructure/utility/awaitable_helpers.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
```

### 7.4 Eliminar macros BIND
```bash
# Buscar y eliminar:
grep -r "BIND[0-9]" src/
grep -r "SEND[0-9]" src/
grep -r "SUBSCRIBE[0-9]" src/
```

**Entregables Fase 7**:
- [x] node::sessions eliminados
- [x] node::protocols eliminados
- [x] network::sessions COMENTADOS (listos para eliminar)
- [x] network::protocols legacy COMENTADOS (listos para eliminar)
- [x] network::core legacy COMENTADOS (p2p.cpp, proxy.cpp, channel.cpp, etc.)
- [x] test/p2p.cpp COMENTADO (TODO: migrar tests a p2p_node.cpp antes de eliminar)
- [ ] infrastructure legacy (sequencer, dispatcher, etc.) pendiente
- [ ] Macros BIND/SEND/SUBSCRIBE pendiente eliminar
- [ ] Documentación actualizada

---

## FASE 5b: Protocolos de Blockchain (PENDIENTE)

### Estado Actual
`protocols_coro.hpp` tiene implementados solo protocolos de **capa de red**:
- ✅ `perform_handshake()` - intercambio version/verack
- ✅ `run_ping_pong()` - keepalive
- ✅ `request_addresses()` / `send_addresses()` - intercambio de peers

### Protocolos Faltantes (Capa de Blockchain)

**IMPORTANTE**: Sin estos protocolos, el nodo se conecta pero NO sincroniza:

#### 1. Block Sync Protocol
```cpp
// Mensajes: getblocks, inv, getdata, block
awaitable<code> sync_blocks(peer_session& peer, block_chain& chain);
awaitable<code> handle_block_announcement(peer_session& peer, inv_message const& inv);
awaitable<code> request_blocks(peer_session& peer, std::vector<hash_digest> const& hashes);
```

#### 2. Header Sync Protocol (Headers-First)
```cpp
// Mensajes: getheaders, headers
awaitable<code> sync_headers(peer_session& peer, block_chain& chain);
awaitable<std::expected<headers_message, code>> request_headers(
    peer_session& peer,
    hash_digest const& from_hash,
    hash_digest const& stop_hash);
```

#### 3. Transaction Relay Protocol
```cpp
// Mensajes: inv, getdata, tx, mempool
awaitable<code> relay_transaction(peer_session& peer, transaction const& tx);
awaitable<code> handle_tx_announcement(peer_session& peer, inv_message const& inv);
awaitable<code> request_mempool(peer_session& peer);  // BIP 35
```

#### 4. Double Spend Proof Protocol (BCH específico)
```cpp
// Mensajes: dsproof-beta
awaitable<code> handle_dsproof(peer_session& peer, dsproof_message const& dsp);
awaitable<code> relay_dsproof(peer_session& peer, dsproof const& proof);
```

### Archivos Legacy de Referencia
Usar como referencia para la implementación moderna:
- `src/node/protocols/protocol_block_in.cpp` (eliminado, ver git history)
- `src/node/protocols/protocol_block_out.cpp` (eliminado, ver git history)
- `src/node/protocols/protocol_header_sync.cpp` (eliminado, ver git history)
- `src/node/protocols/protocol_transaction_in.cpp` (eliminado, ver git history)
- `src/node/protocols/protocol_transaction_out.cpp` (eliminado, ver git history)

### Dependencias
Estos protocolos requieren acceso a:
- `block_chain` para validar y almacenar bloques/headers
- `transaction_pool` (mempool) para transacciones
- Configuración de la red (checkpoints, etc.)

### Ubicación Sugerida
Crear nuevo archivo: `src/network/include/kth/network/blockchain_protocols_coro.hpp`
O extender `protocols_coro.hpp` con una sección separada

---

## FASE 8: Integration Tests

### Problema con Tests Unitarios de Networking

Los tests unitarios tienen limitaciones para probar networking real:
- **Timeouts largos**: Tests de timeout/retry pueden tardar minutos
- **Dependencias externas**: Tests contra nodos reales son flaky
- **Cobertura limitada**: No se pueden testear edge cases de red fácilmente
- **CI/CD lento**: Tests de integración alargan el pipeline

### Solución: Mock Node + Integration Test Suite

Crear un **nodo Bitcoin mockeado** en Python que simule un peer de la red, permitiendo:
- Tests locales rápidos y determinísticos
- Simulación de errores de red y timeouts
- Validación de protocolo end-to-end
- Escenarios de edge cases controlados

### Arquitectura Propuesta

```
┌─────────────────────────────────────────────────────────────┐
│                    Integration Test Suite                    │
│                      (Python/pytest)                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐    TCP/IP     ┌──────────────────────┐   │
│  │              │◄─────────────►│                      │   │
│  │  Knuth Node  │               │  Mock Bitcoin Node   │   │
│  │   (C++)      │               │     (Python)         │   │
│  │              │               │                      │   │
│  └──────────────┘               └──────────────────────┘   │
│                                                              │
│  - Binario compilado            - Simula protocolo Bitcoin  │
│  - Configuración de test        - Respuestas programables   │
│  - Puerto configurable          - Inyección de errores      │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Mock Node Features

```python
# tests/integration/mock_node.py

class MockBitcoinNode:
    """Nodo Bitcoin mockeado para integration tests."""

    def __init__(self, port: int = 18444):
        self.port = port
        self.connections = []
        self.message_log = []

    async def start(self):
        """Iniciar servidor TCP."""
        pass

    async def stop(self):
        """Detener servidor."""
        pass

    # =========================================================================
    # Comportamiento Programable
    # =========================================================================

    def on_version(self, handler: Callable):
        """Registrar handler para mensaje version."""
        pass

    def respond_with(self, message_type: str, payload: bytes):
        """Programar respuesta automática."""
        pass

    def delay_response(self, message_type: str, seconds: float):
        """Simular latencia."""
        pass

    def drop_connection_after(self, message_type: str):
        """Simular desconexión."""
        pass

    def send_invalid_magic(self):
        """Enviar mensaje con magic incorrecto."""
        pass

    def send_invalid_checksum(self):
        """Enviar mensaje con checksum corrupto."""
        pass

    def send_oversized_payload(self, size: int):
        """Enviar payload más grande que el límite."""
        pass

    # =========================================================================
    # Assertions
    # =========================================================================

    def assert_received(self, message_type: str, timeout: float = 5.0):
        """Verificar que se recibió un mensaje."""
        pass

    def assert_connection_count(self, expected: int):
        """Verificar número de conexiones."""
        pass
```

### Casos de Test Propuestos

#### 9.1 Happy Path Tests
```python
# tests/integration/test_handshake.py

@pytest.mark.integration
async def test_successful_handshake():
    """Knuth completa handshake correctamente."""
    async with MockBitcoinNode() as mock:
        mock.respond_with("version", make_version_message())
        mock.respond_with("verack", b"")

        knuth = await start_knuth_node(connect_to=mock.address)

        mock.assert_received("version")
        mock.assert_received("verack")
        assert knuth.connection_count() == 1

@pytest.mark.integration
async def test_ping_pong():
    """Knuth responde a ping con pong."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        mock.send("ping", nonce=12345)
        mock.assert_received("pong", nonce=12345)

@pytest.mark.integration
async def test_addr_exchange():
    """Knuth intercambia direcciones correctamente."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        mock.send("getaddr")
        mock.assert_received("addr")
```

#### 9.2 Error Handling Tests
```python
# tests/integration/test_errors.py

@pytest.mark.integration
async def test_invalid_magic_disconnects():
    """Knuth desconecta al recibir magic inválido."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        mock.send_invalid_magic()

        await asyncio.sleep(0.5)
        assert mock.connection_count() == 0

@pytest.mark.integration
async def test_invalid_checksum_disconnects():
    """Knuth desconecta al recibir checksum inválido."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        mock.send_invalid_checksum()

        await asyncio.sleep(0.5)
        assert mock.connection_count() == 0

@pytest.mark.integration
async def test_oversized_payload_disconnects():
    """Knuth desconecta al recibir payload muy grande."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        mock.send_oversized_payload(100 * 1024 * 1024)  # 100 MB

        await asyncio.sleep(0.5)
        assert mock.connection_count() == 0
```

#### 9.3 Timeout Tests
```python
# tests/integration/test_timeouts.py

@pytest.mark.integration
async def test_handshake_timeout():
    """Knuth desconecta si handshake tarda demasiado."""
    async with MockBitcoinNode() as mock:
        # No responder a version
        mock.on_version(lambda: None)

        knuth = await start_knuth_node(
            connect_to=mock.address,
            handshake_timeout=2
        )

        await asyncio.sleep(3)
        assert mock.connection_count() == 0

@pytest.mark.integration
async def test_inactivity_timeout():
    """Knuth desconecta después de inactividad."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        # No enviar nada por un rato
        await asyncio.sleep(mock.inactivity_timeout + 1)

        assert mock.connection_count() == 0
```

#### 9.4 Reconnection Tests
```python
# tests/integration/test_reconnection.py

@pytest.mark.integration
async def test_reconnect_after_disconnect():
    """Knuth reconecta después de desconexión."""
    async with MockBitcoinNode() as mock:
        await complete_handshake(mock)

        # Forzar desconexión
        mock.drop_all_connections()

        # Esperar reconexión
        await asyncio.sleep(5)
        mock.assert_connection_count(1)

@pytest.mark.integration
async def test_connect_to_multiple_peers():
    """Knuth puede conectar a múltiples peers."""
    async with MockBitcoinNode(port=18444) as mock1, \
               MockBitcoinNode(port=18445) as mock2:

        knuth = await start_knuth_node(
            seeds=[mock1.address, mock2.address]
        )

        await asyncio.sleep(5)
        assert knuth.connection_count() >= 2
```

### Estructura de Directorios

```
tests/
├── unit/                      # Tests unitarios (C++, Catch2)
│   ├── infrastructure/
│   ├── domain/
│   └── network/
│
└── integration/               # Integration tests (Python)
    ├── conftest.py           # Fixtures compartidos
    ├── mock_node/
    │   ├── __init__.py
    │   ├── server.py         # MockBitcoinNode
    │   ├── protocol.py       # Serialización de mensajes
    │   └── messages.py       # Builders de mensajes
    │
    ├── test_handshake.py     # Happy path
    ├── test_errors.py        # Error handling
    ├── test_timeouts.py      # Timeouts
    ├── test_reconnection.py  # Reconnection logic
    └── test_protocols.py     # Protocol-specific tests
```

### Ejecución

```bash
# Tests unitarios (rápidos, en CI)
cmake --build build --target test

# Integration tests (más lentos, manual o nightly)
cd tests/integration
pytest -v -m integration

# Solo happy path (para desarrollo)
pytest -v -m "integration and not slow"

# Con coverage de protocolos
pytest -v --protocol-coverage
```

### Entregables Fase 8

- [ ] `MockBitcoinNode` en Python con protocolo básico
- [ ] Fixtures para pytest (`conftest.py`)
- [ ] Tests de handshake (version/verack)
- [ ] Tests de ping/pong
- [ ] Tests de errores (magic, checksum, payload)
- [ ] Tests de timeouts (handshake, inactivity, expiration)
- [ ] Tests de reconexión
- [ ] **Tests de IPv4 e IPv6**: Verificar que el nodo funciona correctamente con ambas familias de direcciones
  - [ ] Conexiones entrantes IPv4
  - [ ] Conexiones entrantes IPv6
  - [ ] Conexiones salientes IPv4
  - [ ] Conexiones salientes IPv6
  - [ ] Dual-stack (IPv6 aceptando IPv4-mapped addresses)
- [ ] Script de ejecución para CI (nightly)
- [ ] Documentación de cómo agregar nuevos tests

---

## Timeline Estimado

| Fase | Descripción | Complejidad | Riesgo |
|------|-------------|-------------|--------|
| 0 | Preparación | Baja | Bajo |
| 1 | Infraestructura Base | Media | Bajo |
| 2 | Proxy/Channel | Alta | Medio |
| 3 | Connector/Acceptor | Media | Bajo |
| 4 | Session Management | Media | Medio |
| 5 | Protocols | Alta | Medio |
| 6 | P2P Node | Alta | Alto |
| 7 | Integración | Media | Alto |
| 8 | Cleanup | Baja | Bajo |
| 9 | Integration Tests | Media | Bajo |

**Notas**:
- Cada fase puede deployarse independientemente (feature flag)
- La fase 7 es el punto de no retorno
- Mantener backwards compatibility hasta fase 8

---

## Métricas de Éxito

1. **Reducción de código**: 50%+ menos líneas en network/infrastructure
2. **Rendimiento**: Sin regresión (idealmente mejora de 5-10%)
3. **Mantenibilidad**: Reducción de callback depth de 4-6 a 0
4. **Bugs**: Sin nuevos bugs críticos en migración
5. **Tests**: Cobertura mantenida o mejorada

---

## Riesgos y Mitigaciones

| Riesgo | Probabilidad | Impacto | Mitigación |
|--------|--------------|---------|------------|
| Bugs sutiles en coroutines | Media | Alto | Tests exhaustivos, rollback via feature flag |
| Performance regression | Baja | Medio | Benchmarks en cada fase |
| Breaking changes en Asio experimental | Media | Medio | Pinear versión de Asio |
| Compiladores no soportados | Baja | Alto | Mantener código legacy con #ifdef |

---

## Orden de Implementación Recomendado

```
Fase 0 ─────────────────────────────────────────►
        │
        ▼
Fase 1 (thread_pool, helpers) ─────────────────►
        │
        ▼
Fase 2 (peer_session) ─────────────────────────►
        │
        ├────► Fase 3 (connector/acceptor)
        │
        ▼
Fase 4 (peer_manager) ─────────────────────────►
        │
        ▼
Fase 5 (protocols) ────────────────────────────►
        │
        ▼
Fase 6 (p2p_node) ─────────────────────────────►
        │
        ▼
Fase 7 (integración) ──────────────────────────►
        │
        ▼
Fase 8 (cleanup) ──────────────────────────────►
```

---

## Próximos Pasos Inmediatos

1. **Verificar requisitos**: Comprobar versiones de compiladores y Asio
2. **Crear branch**: `feature/coroutine-migration`
3. **Implementar Fase 0**: Feature flag y smoke test
4. **Comenzar Fase 1**: thread_pool moderno y helpers

¿Empezamos con la Fase 0?

---

## Fase 7: Integración con full_node (Detallado)

### Estado Actual

Actualmente `full_node` hereda de `network::p2p`:

```cpp
class full_node
    : public multi_crypto_setter
    , public network::p2p   // <-- Herencia problemática
{
    // ...
};
```

Este diseño tiene varios problemas:
1. **Acoplamiento fuerte**: full_node depende de la implementación interna de p2p
2. **Métodos virtuales innecesarios**: No hay polimorfismo real (no hay código que use `p2p*` para manejar un `full_node*`)
3. **Complejidad**: Las sessions de node heredan de sessions de network solo para hacer override de `attach_protocols()`

### Análisis de Sessions de Node

Las tres sessions de node (`session_inbound`, `session_outbound`, `session_manual`) son **idénticas** en su método `attach_protocols()`:

```cpp
void session_XXX::attach_protocols(channel::ptr channel) {
    auto const version = channel->negotiated_version();

    // Protocolos de red (de network::)
    if (version >= version::level::bip31) {
        attach<protocol_ping_60001>(channel)->start();
    } else {
        attach<protocol_ping_31402>(channel)->start();
    }
    if (version >= domain::message::version::level::bip61) {
        attach<protocol_reject_70002>(channel)->start();
    }
    attach<protocol_address_31402>(channel)->start();

    // Protocolos de blockchain (de node::)
    attach<protocol_block_in>(channel, chain_)->start();
    attach<protocol_block_out>(channel, chain_)->start();
    attach<protocol_double_spend_proof_in>(channel, chain_)->start();
    attach<protocol_double_spend_proof_out>(channel, chain_)->start();
    attach<protocol_transaction_in>(channel, chain_)->start();
    attach<protocol_transaction_out>(channel, chain_)->start();
}
```

### Análisis de Protocolos de Node

Los protocolos de node heredan de clases base de network:
- `protocol_block_in` : `network::protocol_timer`
- `protocol_block_out` : `network::protocol_events`
- `protocol_block_sync` : `network::protocol_timer`
- `protocol_header_sync` : `network::protocol_timer`
- `protocol_transaction_in` : `network::protocol_events`
- `protocol_transaction_out` : `network::protocol_events`
- `protocol_double_spend_proof_in` : `network::protocol_events`
- `protocol_double_spend_proof_out` : `network::protocol_events`

Todos usan el patrón callback-based heredado.

### Nuevo Diseño: Composición en lugar de Herencia

```cpp
class full_node {
private:
    network::p2p_node network_;      // Composición en lugar de herencia
    blockchain::block_chain chain_;

public:
    full_node(configuration const& config)
        : network_(config.network)
        , chain_(...)
    {}

    // Delegación explícita
    ::asio::awaitable<code> start() {
        if (!chain_.start()) {
            co_return error::operation_failed;
        }
        co_return co_await network_.start();
    }

    ::asio::awaitable<code> run() {
        co_return co_await network_.run();
    }

    void stop() {
        network_.stop();
        chain_.stop();
    }

    // Acceso a componentes
    network::p2p_node& network() { return network_; }
    blockchain::safe_chain& chain() { return chain_; }
};
```

### Protocolos Coroutine Existentes vs Por Crear

Ya existe `network::protocols_coro` (creado en Fase 5) con protocolos de red genéricos:
- `perform_handshake()` - version/verack
- `run_ping_pong()` - ping/pong keepalive
- `request_addresses()` / `send_addresses()` - getaddr/addr

El nuevo `node::protocols_coro` extenderá este patrón para protocolos de blockchain.

### Plan de Implementación

#### Paso 1: Crear protocol handlers con coroutines en node

Siguiendo el patrón de `network::protocols_coro`, crear funciones coroutine para protocolos de blockchain:

```cpp
// src/node/include/kth/node/protocols_coro.hpp

namespace kth::node {

/// Run all blockchain protocols for a peer
::asio::awaitable<void> run_blockchain_protocols(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

/// Block reception protocol
::asio::awaitable<void> run_block_in_protocol(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

/// Block sending protocol
::asio::awaitable<void> run_block_out_protocol(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

/// Transaction reception protocol
::asio::awaitable<void> run_transaction_in_protocol(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

/// Transaction sending protocol
::asio::awaitable<void> run_transaction_out_protocol(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

/// DSP protocols
::asio::awaitable<void> run_dsp_in_protocol(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

::asio::awaitable<void> run_dsp_out_protocol(
    network::peer_session& peer,
    blockchain::safe_chain& chain);

} // namespace kth::node
```

#### Paso 2: Modificar full_node para usar composición

```cpp
// full_node.hpp
class full_node {
public:
    explicit full_node(configuration const& config);
    ~full_node();

    // Lifecycle - ahora async
    ::asio::awaitable<code> start();
    ::asio::awaitable<code> run();
    void stop();
    void join();

    // Properties
    network::settings const& network_settings() const;
    blockchain::settings const& chain_settings() const;
    node::settings const& node_settings() const;
    blockchain::safe_chain& chain();
    network::p2p_node& network();

private:
    // Composición - NO herencia
    network::p2p_node network_;
    blockchain::block_chain chain_;

    node::settings const& node_settings_;
    blockchain::settings const& chain_settings_;
};
```

#### Paso 3: Configurar protocol handlers en p2p_node

`p2p_node` necesita un mecanismo para que el usuario configure qué protocolos ejecutar con cada peer:

```cpp
// Opción A: Callback de configuración
using protocol_configurator = std::function<::asio::awaitable<void>(peer_session&)>;

p2p_node::set_protocol_handler(protocol_configurator handler);

// Opción B: Template/Strategy
class full_node {
    ::asio::awaitable<void> on_peer_connected(network::peer_session::ptr peer) {
        // Ejecutar protocolos de network
        co_await network::run_ping_pong(*peer, ping_interval);

        // Ejecutar protocolos de blockchain
        co_await node::run_blockchain_protocols(*peer, chain_);
    }
};
```

#### Paso 4: Eliminar sessions y protocols legacy de node

Una vez que full_node use composición y los nuevos protocols coro:

1. Eliminar `src/node/sessions/session_inbound.hpp/.cpp`
2. Eliminar `src/node/sessions/session_outbound.hpp/.cpp`
3. Eliminar `src/node/sessions/session_manual.hpp/.cpp`
4. Eliminar `src/node/sessions/session_block_sync.hpp/.cpp`
5. Eliminar `src/node/sessions/session_header_sync.hpp/.cpp`

Los protocolos legacy se mantienen temporalmente y se migran gradualmente.

### Dependencias

```
full_node
    │
    ├──► network::p2p_node (composición)
    │       │
    │       ├──► peer_manager
    │       │       └──► peer_session[]
    │       │
    │       ├──► hosts (address management)
    │       │
    │       └──► protocols_coro (handshake, ping, address)
    │
    └──► blockchain::block_chain
            │
            └──► node::protocols_coro (block, tx, dsp)
```

### Archivos a Modificar/Crear

| Archivo | Acción | Descripción |
|---------|--------|-------------|
| `src/node/include/kth/node/full_node.hpp` | Modificar | Cambiar herencia a composición |
| `src/node/src/full_node.cpp` | Modificar | Adaptar a nuevo diseño |
| `src/node/include/kth/node/protocols_coro.hpp` | Crear | Nuevos protocols coroutine |
| `src/node/src/protocols_coro.cpp` | Crear | Implementación |
| `src/network/include/kth/network/p2p_node.hpp` | Modificar | Agregar hook para protocols |

### Archivos Legacy a Eliminar (Fase Cleanup)

#### Network Layer - Protocolos Legacy (reemplazados por `network::protocols_coro`)

| Archivo Legacy | Reemplazado Por |
|----------------|-----------------|
| `src/network/include/kth/network/protocols/protocol.hpp` | Base class - ya no necesaria |
| `src/network/src/protocols/protocol.cpp` | |
| `src/network/include/kth/network/protocols/protocol_events.hpp` | Base class - ya no necesaria |
| `src/network/src/protocols/protocol_events.cpp` | |
| `src/network/include/kth/network/protocols/protocol_timer.hpp` | Base class - ya no necesaria |
| `src/network/src/protocols/protocol_timer.cpp` | |
| `src/network/include/kth/network/protocols/protocol_version_31402.hpp` | `perform_handshake()` |
| `src/network/src/protocols/protocol_version_31402.cpp` | |
| `src/network/include/kth/network/protocols/protocol_version_70002.hpp` | `perform_handshake()` |
| `src/network/src/protocols/protocol_version_70002.cpp` | |
| `src/network/include/kth/network/protocols/protocol_ping_31402.hpp` | `run_ping_pong()` |
| `src/network/src/protocols/protocol_ping_31402.cpp` | |
| `src/network/include/kth/network/protocols/protocol_ping_60001.hpp` | `run_ping_pong()` |
| `src/network/src/protocols/protocol_ping_60001.cpp` | |
| `src/network/include/kth/network/protocols/protocol_address_31402.hpp` | `request_addresses()` |
| `src/network/src/protocols/protocol_address_31402.cpp` | |
| `src/network/include/kth/network/protocols/protocol_seed_31402.hpp` | `send_addresses()` |
| `src/network/src/protocols/protocol_seed_31402.cpp` | |
| `src/network/include/kth/network/protocols/protocol_reject_70002.hpp` | Deprecated (BIP 61) |
| `src/network/src/protocols/protocol_reject_70002.cpp` | |

#### Node Layer - Sessions Legacy (reemplazadas por composición en `full_node`)

| Archivo Legacy | Notas |
|----------------|-------|
| `src/node/include/kth/node/sessions/session_inbound.hpp` | Solo hacía override de `attach_protocols()` |
| `src/node/src/sessions/session_inbound.cpp` | |
| `src/node/include/kth/node/sessions/session_outbound.hpp` | Solo hacía override de `attach_protocols()` |
| `src/node/src/sessions/session_outbound.cpp` | |
| `src/node/include/kth/node/sessions/session_manual.hpp` | Solo hacía override de `attach_protocols()` |
| `src/node/src/sessions/session_manual.cpp` | |
| `src/node/include/kth/node/sessions/session_block_sync.hpp` | Sincronización de bloques |
| `src/node/src/sessions/session_block_sync.cpp` | |
| `src/node/include/kth/node/sessions/session_header_sync.hpp` | Sincronización de headers |
| `src/node/src/sessions/session_header_sync.cpp` | |

#### Node Layer - Protocolos Legacy (reemplazados por `node::protocols_coro`)

| Archivo Legacy | Reemplazado Por |
|----------------|-----------------|
| `src/node/include/kth/node/protocols/protocol_block_in.hpp` | `run_block_in_protocol()` |
| `src/node/src/protocols/protocol_block_in.cpp` | |
| `src/node/include/kth/node/protocols/protocol_block_out.hpp` | `run_block_out_protocol()` |
| `src/node/src/protocols/protocol_block_out.cpp` | |
| `src/node/include/kth/node/protocols/protocol_block_sync.hpp` | `run_block_sync_protocol()` |
| `src/node/src/protocols/protocol_block_sync.cpp` | |
| `src/node/include/kth/node/protocols/protocol_header_sync.hpp` | `run_header_sync_protocol()` |
| `src/node/src/protocols/protocol_header_sync.cpp` | |
| `src/node/include/kth/node/protocols/protocol_transaction_in.hpp` | `run_transaction_in_protocol()` |
| `src/node/src/protocols/protocol_transaction_in.cpp` | |
| `src/node/include/kth/node/protocols/protocol_transaction_out.hpp` | `run_transaction_out_protocol()` |
| `src/node/src/protocols/protocol_transaction_out.cpp` | |
| `src/node/include/kth/node/protocols/protocol_double_spend_proof_in.hpp` | `run_dsp_in_protocol()` |
| `src/node/src/protocols/protocol_double_spend_proof_in.cpp` | |
| `src/node/include/kth/node/protocols/protocol_double_spend_proof_out.hpp` | `run_dsp_out_protocol()` |
| `src/node/src/protocols/protocol_double_spend_proof_out.cpp` | |

### Riesgos

1. **Breaking change API**: La API pública de full_node cambia
2. **Migración de protocols**: Los protocols legacy usan muchos callbacks internos
3. **Sincronización**: block_sync y header_sync son complejos

### Mitigación

- Mantener compatibilidad parcial con `#ifdef` durante transición
- Migrar protocols uno a uno, probando cada uno
- Dejar sync protocols para el final (o fase separada)

---

## Pendientes

### Sync & Download

- [ ] **Descarga paralela de bloques desde múltiples peers**
  - Actualmente la descarga de bloques es secuencial (~5-11 bloques/segundo)
  - BCHN usa hasta 16 bloques en vuelo por peer, con hasta 8 peers (~128 bloques simultáneos)
  - Esto reduciría el tiempo de sync de ~23-46 horas a ~1-2 horas
  - Requiere coordinar qué rangos de bloques pide cada peer
  - Referencia: BCHN `net_processing.cpp` - `MAX_BLOCKS_IN_TRANSIT_PER_PEER`

- [ ] **Agregar flag `is_outbound` a `peer_session`**
  - Para preferir peers outbound en la selección de sync (como hace BCHN con `fPreferredDownload`)

### IPv6

- [ ] **Investigar problemas con resolución IPv6**
  - Actualmente filtrado a IPv4-only en `hosts.cpp` (ver TODO en el código)
  - ASIO resolver falla con "Host not found" para muchas direcciones IPv6
  - ¿Es un problema de configuración del resolver o las direcciones son inválidas?

