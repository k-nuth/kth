# version 0.11.0
- Improved continuous integration.
- Fix delay between incoming conections.
- Basic wallet classes added to bitprim-core.

# version 0.10.2

- BIP activation's height fixed for Litecoin
- Added mempool transaction function to bitprim-node-cint

# version 0.10.1

- Build fixes for Litecoin.
- Support for protocol version 70015.
- Bitprim-core scripting now supports BCH signatures when using it as a library.
- Node-cint functionality to create raw transactions.
- Bitprim binary logs if it was built on release or debug.
- Fix on vout index when requesting information about mempool's transactions.
- Block and transactions added stop guards.

# version 0.10.0

- BIP0141 fixes for BTC and LTC.
- BIP0152 fixes for all currencies.
- Updated BCH consensus to behave like the ABC.
- P2P message fix for bitnodes counter.
- The default value for minimum fee on incoming transactions changed to 0.1 statoshis/byte. This value can be changes in the configuration file.

# version 0.9.1

- The database structure was updated to support blocks with more than 65535 transactions. (The new structure is only used when Bitprim is compiled for Bitcoin Cash).
- Fixed a bug where some transactions were not being writen to the history table when downloading the blockchain using checkpoints.

# version 0.9.0

- [Bitcoin Cash](https://www.bitcoincash.org/) 2018-May-15 hard fork changes:
    - Maximum block size increased to 32 MB.
    - ​Script op-codes added or reactivated: _OP_CAT_, _OP_AND_, _OP_OR_, _OP_XOR_, _OP_DIV_, _OP_MOD_, _OP_SPLIT_, _OP_SUBSTR_, _OP_NUM2BIN_, and _OP_BIN2NUM_.

    More details in [Bitcoin Cash 2018-May-15 hard fork](https://github.com/bitprim/bitprim/blob/master/doc/bch-announces/HF-2018-may-15.md).


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

