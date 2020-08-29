# version 0.4.1

You can install Knuth node version 0.4.1 [using these instructions](https://kth.cash/#download).

This release includes the following features and fixes:

- Updated several external dependencies: Boost libs. 1.74.0, {fmt} lib. 7.0.3, spdlog lib. 1.7.0, catch2 testing framework 2.13.0.
- Some bug fixes and code style improvements.

# version 0.4.0

You can install Knuth node version 0.4.0 [using these instructions](https://kth.cash/#download).

This release includes the following features and fixes:

- Implementation of aserti3-2d: the new difficulty adjustment algorithm which will be activated on November 15, 2020.
- Fixed a linking error on Debian10 using GCC8, trying to link std::filesystem.
- Some minor fixes and code style improvements.

# version 0.3.2

You can install Knuth node version 0.3.2 [using these instructions](https://kth.cash/#download).

This release includes the following features and fixes:

- Most of the modules' code underwent a major modernization.
- All unit tests were improved as a previous step to meet our mining compliance test objective.
- The use of the Boost.Test framework has been replaced by the more modern Catch2.

# version 0.3.1

You can install Knuth node version 0.3.1 [using these instructions](https://kth.cash/#download).

This release includes the following features and fixes:

- Fix a runtime error when parsing config file in testnet mode.
- Fix BCH seeders.
- Fix algorithm package version.

# version 0.3.0

You can install Knuth node version 0.3.0 [using these instructions](https://kth.cash/#download).

This release includes the following features and fixes:

- Post HF stability changes.
- DB performance improvements.
- Use std::filesystem instead of boost::filesystem.
- Use std::optional instead of boost::optional.
- Usage of TaoCpp algorithms library (as a dependency).
- Optional and experimental usage of a new logging library.
- Several improvements in the build system and continuous integration.

# version 0.2.0

### Network upgrade

At the MTP time of 1589544000 (May 15, 2020 12:00:00 UTC) the following behaviors will change:

- `OP_REVERSEBYTES` support in script.  

- New SigOps counting method (SigChecks) as standardness and consensus rules.

- Regarding IFP: For exchanges and users, this client will follow the longest chain whether it includes IFP soft forks or not. For miners, running this client ensures the `getblocktemplate` RPC call will not automatically insert IFP white-list addresses into the coinbase transaction.

### Other changes

- New DB mode: Read-only.
- DB performance improvements.
- Binary Logging (Experimental and optional feature).
- New DB module (Experimental and optional feature).
- New command line UI (Work in progress, experimental and optional feature).
- Implement `xversion` message (Work in progress, experimental).
- Removal of external Conan repositories.
- Usage of new formater library.
- Various bug fixes and stability improvements.
- Code style improvements.

# version 0.1.0

- First release
- [Bitcoin Cash (BCH)](https://www.bitcoincash.org/) 2019-Nov-15 hard fork support (Multisig Schnorr Signatures).

