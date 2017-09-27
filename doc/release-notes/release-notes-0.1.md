# Notable changes

## Build infrastructure

The [Conan](https://www.conan.io/) package manager is now used to handle all of bitprim's projects internal and external dependencies, as well as installation. Also, continuous integration was implemented using [Travis](https://travis-ci.org/) for Linux and OSX builds and [Appveyor](https://www.appveyor.com/) for Windows builds.

## C API

The new bitprim-node-cint project works as a C interface for all of bitprim's features. The goal of this C interface is to build bindings for other programming languages on top of it, since most popular languages such as Python, Golang, Javascript and C# interface easily with C.

--------------------

# 0.1 changelog

- Conan infrastructure + Bintray repositories for easier installation (Linux, OSX, and Windows)
- New project: *bitprim-node-cint*. Implements a C interface for the node. In this version, the following functions are exposed:
    - fetch_last_height
    - fetch_block_height
    - fetch_block_header_by_hash
    - fetch_block_by_height
    - fetch_block_by_hash
    - fetch_merkle_block_by_height
    - fetch_merkle_block_by_hash
    - fetch_transaction
    - fetch_compact_block_by_height
    - fetch_compact_block_by_hash
    - fetch_transaction_position
    - fetch_spend
    - fetch_history
    - validate_transaction
    - fetch_stealth

All *fetch* functions are asynchronous, i.e. they return immediately and require a callback parameter to return the result. Also, each of these functions have a *get* sister function, which works synchronously, i.e. blocks until result is retrieved and returned directly.

