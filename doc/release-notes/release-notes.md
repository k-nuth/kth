# version 0.8.0

- Crypto currencies are selected at compile-time.
- Json-RPC API performance improvements.
- Some improvements and fixes in the C-API to support our new Insight-API.
- Binary packaging improvements.

# version 0.7.0

- Improved RPC getblocktemplate response time.
- Implemented RPC getaddressmempool command.
- Some improvements and fixes in the C-API to support our new Insight-API.
- Binary packaging improvements.

# version 0.6.0

- Added new RPC functionality.
- Fix in the historical database.
- Logging format now includes the date.


# version 0.5.0

- Added support for the new [Bitcoin Cash](https://www.bitcoincash.org/)'s addresses format, [Spec. here](https://github.com/Bitcoin-UAHF/spec/blob/master/cashaddr.md).
- Added support for the new Bitcoin Cash's "netmagic".
- Fixed some issues on RPC module.


# version 0.4.0

- [JSON-RPC](https://en.wikipedia.org/wiki/JSON-RPC) support, for mining, wallet and explorers.  
- Minor bug fixes.


# version 0.3.0

- Updated the [Bitcoin Cash](https://www.bitcoincash.org/) Difficulty Adjustment Algorithm (DAA), activates on November 13th.  
- Minor bug fixes


# version 0.2.0

- [Bitcoin cash](https://www.bitcoincash.org/) support
- Networking bug fixes
- Merged with [libbitcoin](https://github.com/libbitcoin/libbitcoin) 3.3.0


# version 0.1.0

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
    - fetch_transaction_position
    - fetch_spend
    - fetch_history
    - validate_transaction
    - fetch_stealth
- Aside from the chain functions above, functions are available for operating with all their related data structures:
    - block_indexes
    - block
    - header
    - history
    - input
    - merkle_block
    - output
    - payment_address
    - point
    - script
    - transaction

All *fetch* functions are asynchronous, i.e. they return immediately and require a callback parameter to return the result. Also, each of these functions have a *get* sister function, which works synchronously, i.e. blocks until result is retrieved and returned directly.

