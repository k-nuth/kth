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

