# Async mining-info C boundary regression

This control compiles the repository's actual `chain_mining.cpp` and public C
headers with real standalone Asio. Only the chain implementation, synchronous
wait and field-conversion helpers are replaced by small controlled doubles.
It does not claim to validate the database, mining snapshot computation or full
chain integration.

Native builds with `ENABLE_TEST` build the control and register
`kth_capi_mining_info_callback` with CTest. The test resolves standalone Asio
with `find_package(asio REQUIRED)` in its own directory, independently of the
node transport selection. A missing dependency fails configuration. The existing native CI CTest runs
therefore execute it automatically, without a separate Python compiler setup.

For a focused run after a normal build:

```sh
ctest --test-dir /path/to/build -R '^kth_capi_mining_info_callback$' --output-on-failure
```

The standalone runner remains available for baseline comparisons:

```sh
python3 test/c-api-mining-info/run.py --asio-include /path/to/asio/include
```

Use `--source-root /path/to/unmodified/master` to run the same control against
the original implementation. It fails because an exception in the query ends
the detached coroutine without delivering the terminal callback.

The corrected boundary must deliver exactly one callback with the original
chain/context, preserve successful fields, propagate a domain failure and map
both coroutine and startup exceptions to C error codes with a zeroed payload.
No background threads or borrowed state outlive the test's event loop.

HeaderSync and chain_submission remain
handwritten and untouched. Existing C function signatures, enum values and POD
layouts are unchanged.
