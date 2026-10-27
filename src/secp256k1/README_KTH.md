# Vendored secp256k1

The upstream source is bitcoin-core/secp256k1 **v0.8.0**, commit
`6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d`.

The `src`, `include`, `cmake` and `contrib` trees, CMake entry point, license,
package template and changelog come from that release. Upstream build workflows,
Autotools files, examples and other documentation are not included.

Local changes retain the Bitcoin Cash Schnorr scheme, which is distinct from
upstream's BIP340 `schnorrsig` module:

- `include/secp256k1_schnorr.h` and `src/modules/schnorr/` contain the BCH API,
  implementation, known-answer vectors and randomized tests.
- `CMakeLists.txt` adds the `SECP256K1_ENABLE_MODULE_SCHNORR_BCH` option.
- `src/secp256k1.c` includes the BCH implementation when enabled.
- `src/tests.c` registers its tests with the upstream test framework.

The BCH module uses the context's SHA256 implementation for its challenge and
built-in RFC6979 nonce generation. User-supplied nonce functions retain their
existing callback contract. Tests check that a custom SHA256 compression
implementation produces identical signatures and is used by signing and
verification.

Knuth's parent CMake configuration selects the enabled modules and installs
their public headers. MuSig and Silent Payments remain disabled. For future
updates, preserve the BCH test registry as well as the production module: the
unit and exhaustive tests, installed-header consumer and existing cryptographic
parity tests must continue to run.
