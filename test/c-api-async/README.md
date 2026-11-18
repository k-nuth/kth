# Async C-API completion and subscription controls

The native project registers `kth_capi_async_callbacks` in CTest whenever native
C-API tests are enabled. The test resolves Asio with `find_package(asio REQUIRED)`
in its own directory, independently of `KTH_ASIO_STANDALONE` and imported target
visibility in sibling directories. The CI registry gate requires it by name.

```sh
cmake --build /path/to/native/build --target kth_capi_async_callbacks_test
ctest --test-dir /path/to/native/build -R '^kth_capi_async_callbacks$' -V
```

The executable compiles the actual `chain_async.cpp`, `chain_other.cpp` and
`block_list.cpp`, with the public C headers, ownership helpers and mining-template
conversion. It uses real Asio coroutines and concurrent channels. Controlled
chain/payload dependencies make domain, startup, coroutine, copy and conversion
failures deterministic; they do not validate a database, consensus or the full
node's shutdown integration.

186 scenarios cover the 17 one-shot operations and three subscriptions:

- Success and domain errors retain their existing data/status behavior.
- Coroutine exceptions, synchronous startup exceptions and input/output copies
  deliver one terminal error with zero/null outputs.
- A test-only global allocation injector fails the real `co_spawn` startup
  allocation. Awaitable-frame recycling is disabled only in this test so that
  allocation failure cannot depend on prior tests' allocator caches.
- Input mutation immediately after submission proves the copied value survives.
- Failure converting the second owned result frees the first; staged block-event
  lists are freed on partial failure. Payload counters check ownership balance.
- Subscriptions retain multiple events, ignore stale block events, close/cancel
  suspended readers, handle shutdown before their receiver starts and report an
  exception after earlier successful events. Registration cleanup precedes the
  terminal error callback.
- Internal tip wake-ups produce no C block callback, while incoming-only,
  replaced-only and allocated-empty block lists retain their ordinary delivery.
  A later terminal error still cleans up the registration before its callback.
- A handler returning zero receives no further callback and may release its
  context immediately. The chain must remain alive through subscription cleanup.

For baseline comparison or a focused sanitizer run:

```sh
python3 test/c-api-async/run.py --asio-include /path/to/asio/include
python3 test/c-api-async/run.py --asio-include /path/to/asio/include \
  --source-root /path/to/unmodified/master
python3 test/c-api-async/run.py --asio-include /path/to/asio/include --sanitize
```

The same control on master after #910 fails for lost callbacks, escaping startup
exceptions, retained subscriptions and partial-result leaks. Handler exceptions
are outside the public C callback contract: handlers must not throw. Allocation
failures map to `kth_ec_out_of_memory`, other internal exceptions to
`kth_ec_unhandled_exception`; domain errors retain their normal mappings.

One-shot factories now return the native awaitable/result. A small value-owning
`async_completion` adapter centralizes coroutine/response-conversion exceptions;
the outer catch handles launch failures through the same adapter. Result delivery
is no longer mixed into the work coroutine. The adapter requires nonthrowing
result moves/default construction because Asio performs those steps outside its
handler. Block submission transports only its error code across that boundary.
Hash query inputs are read after suspension in the double, so the controls also
require each factory's owned argument captures to remain alive.

C function signatures, enum values and POD layouts are unchanged. The remaining
C-API coroutine launches in `chain_mining.cpp` and `chain_query.cpp` already use
exception-aware completion handlers; this change removes the remaining detached
launches from the two handwritten chain files. These focused controls are not a
claim that every asynchronous C-API path or native concurrency route is covered.
