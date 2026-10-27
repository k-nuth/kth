# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Content-based decisions for the upstream cases that no port covers. Every rule below was written after reading the cases it
# names (the case list of a unit file, the docstring, log lines and assertions of a functional file), not from the file name.
# build_matrix.py applies the first rule whose pattern matches the upstream file path, to every entry that still has the generic
# file-level decision, and stops if a file is left without one.
#
# status 'unsupported_feature': the original tests a rule or phase that is externally observable and absent from Knuth.
# status 'excluded': the original tests something that is not a consensus or standardness rule (the objective is stated), or
# is outside the scope of this replication (VM, signatures, benchmarks, fuzzing, storage formats, wallet, RPC formatting).

R = []


def rule(pattern, status, reason, gap=None):
    R.append((pattern, status, reason, gap))


# --- benchmarks and fuzzing --------------------------------------------------------------------------------------------
rule(r'^src/bench/', 'excluded',
     'benchmark: measures the run time of a code path (block assembly, CheckBlock, ccoins cache, duplicate inputs, mempool eviction, '
     'removeForBlock, median time past, merkle root, crypto, json, ...) and asserts no rule')
rule(r'^src/test/fuzz/', 'excluded', 'fuzz target (bloom filters, deserialization, partially downloaded block, rolling bloom, script flags, txrequest): generates inputs and asserts no expected value')

# --- unit tests: VM, signatures, crypto, encodings -------------------------------------------------------------------------
rule(r'^src/test/(script_tests|scriptnum_tests|bigint_tests|bigint_script_property_tests|monolith_opcodes_tests|op_reversebytes_tests|'
     r'schnorr_tests|checkdatasig_tests|sigencoding_tests|sighash_tests|sighashtype_tests|sigcache_tests|sigcheckcount_tests|'
     r'script_bitfield_tests|libauth_tests|vmlimits_tests|native_introspection_tests|key_tests)\.cpp$',
     'excluded', 'script interpreter, numeric opcodes, signature encoding/hash types or key handling (VM and signatures are out of scope): '
     'script_build/script_json_test, CLTV, CHECKMULTISIG, op_*_tests, schnorr/ECDSA encodings, sighash, libauth vectors, vmlimits, '
     'introspection opcodes. Cases of script_tests that are not interpreter steps (IsPushOnly) are handled one by one in build_matrix.py')
rule(r'^src/test/blockstatus_tests\.cpp$', 'excluded',
     'BlockStatus bit-flag container of the BCHN block index: validity level (UNKNOWN .. SCRIPTS), has-data, has-undo, failed, failed-parent, parked and parked-parent flags and their '
     'with*() accessors, checked over all permutations (CheckBlockStatus, CheckAllPermutations). The single case is named sighash_construction_test by copy-paste but tests no SigHashType. '
     'Internal state container; the behaviors it supports (marking a branch invalid, parking) are listed as failed_regression / unsupported_feature under abc-invalid-chains and abc-parkedchain')
rule(r'^src/test/(crypto_tests|hash_tests|uint256_tests|arith_uint256_tests|bswap_tests|bitmanip_tests|lcg_tests|random_tests|'
     r'base32_tests|base58_tests|base64_tests|bip32_tests|compress_tests|ec_multiset_tests)\.cpp$', 'excluded',
     'hash functions, big-integer and bit utilities, random generators, base-N encodings, BIP32 derivation, amount compression (storage) and the EC multiset '
     'commitment used by BCHN indexes: primitives with published vectors, no consensus or standardness rule')
rule(r'^src/test/(cashaddr_tests|cashaddrenc_tests|key_io_tests|dstencode_tests|core_io_tests|bip69_tests|descriptor_tests)\.cpp$', 'excluded',
     'address encodings (CashAddr, token-aware addresses, key I/O), script/transaction text formats (ParseScript, FormatScript, '
     'DecodeTokenDataUV), BIP69 ordering and descriptors: wallet and tooling layers; the token output bytes themselves are in token_tests')

# --- unit tests: storage, indexes, concurrency, utilities ----------------------------------------------------------------
rule(r'^src/test/(dbwrapper_tests|flatfile_tests|blockstorage_tests|txindex_tests|net_datasource_tests|streams_tests|allocator_tests|'
     r'cuckoocache_tests|prevector_tests|span_tests|heapoptional_tests|rwcollection_tests|raii_event_tests)\.cpp$', 'excluded',
     'storage and container infrastructure (LevelDB wrapper, flat files, block file integrity on read, transaction index, streams, arenas, caches, '
     'containers): implementation of BCHN, Knuth uses its own storage with its own tests')
rule(r'^src/test/(deadlock_tests|reverselock_tests|sync_tests|scheduler_tests|checkqueue_tests|util_threadnames_tests|validation_block_tests|logging_tests|'
     r'getarg_tests|sanity_tests|testlib_tests|torcontrol_tests|util_tests|serialize_tests)\.cpp$', 'excluded',
     'locks, threads, scheduler, check queue, signal ordering, logging, argument parsing, string/time utilities and the BCHN serialization framework '
     '(varint storage encoding, floats, vectors, optionals): no consensus or standardness rule. The CompactSize cases `compactsize` and `noncanonical` '
     'of serialize_tests are handled separately')
rule(r'^src/test/skiplist_tests\.cpp$', 'excluded', 'CBlockIndex skip pointers and block locators (BuildSkip, GetLocator, FindEarliestAtLeast): index structure; Knuth header_index has its own skip pointer tests')
rule(r'^src/test/blockchain_tests\.cpp$', 'excluded', 'GetDifficulty() floating point value reported by the getdifficulty RPC for very low to very high targets: RPC number formatting')
rule(r'^src/rpc/test/server_tests\.cpp$', 'excluded', 'RPC server: whether a deprecated RPC is enabled')
rule(r'^src/test/(rpc_tests|rpc_server_tests)\.cpp$', 'excluded',
     'RPC parameter parsing and formatting (rawparams, rawsign, createrawtransaction OP_RETURN data output, monetary values, ban, '
     'getblockstats percentiles) and RPC command dispatch; rpc_submitblock_parallel tests concurrency of the RPC, not a block rule')
rule(r'^src/univalue/', 'excluded', 'UniValue JSON library')

# --- unit tests: p2p, network, peers --------------------------------------------------------------------------------------
rule(r'^src/test/(addrman_tests|net_tests|netbase_tests|seedspec6_tests)\.cpp$', 'excluded',
     'peer address management and network primitives (address manager buckets and collisions, CNetAddr/CSubNet/ServiceFlags parsing and serialization, '
     'banning, permissions, local address selection): no consensus or standardness rule')
rule(r'^src/test/timedata_tests\.cpp$', 'unsupported_feature',
     'network-adjusted time: median of the time offsets reported by peers (util_MedianFilter, addtimedata) that BCHN adds to the local clock '
     'in the block-timestamp future limit (MAX_FUTURE_BLOCK_TIME). Knuth has no peer time adjustment; the future-timestamp rule uses the local clock '
     '(ported in feature_block b48)', 'feature_absent')
rule(r'^src/test/(peerratelimiter_tests|denialofservice_tests|txrequest_tests|inv_tests|extversion_tests)\.cpp$', 'excluded',
     'peer management: per-peer rate limits, misbehavior score and ban time, outbound eviction, transaction download scheduling, inv kinds/commands, '
     'extversion handshake: behavior of the connection with a peer, not a validity rule of a block or transaction')
rule(r'^src/test/(blockencodings_tests|bloom_tests|merkleblock_tests|pmt_tests|blockfilter_tests)\.cpp$', 'excluded',
     'compact block reconstruction (BIP152), bloom filters and filtered blocks with partial merkle trees (BIP37), block filters (BIP157/158): '
     'protocol messages that carry blocks and transactions; the validity of the reconstructed block is judged by the block rules ported elsewhere. '
     'pmt_malleability concerns partial merkle trees of SPV proofs, not full block validation')
rule(r'^src/test/gbtlight_tests\.cpp$', 'excluded', 'getblocktemplatelight: template directory cache and merkle branch for miners (RPC mining protocol)')
rule(r'^src/test/policyestimator_tests\.cpp$', 'unsupported_feature', 'MempoolMinimumFeeEstimate: after 200 blocks of 100 transactions the fee estimate must not be below the mempool rolling minimum fee, and the estimate falls back to the rolling minimum when the mempool grows without blocks. Knuth has neither a fee estimator nor a rolling minimum fee (mempool size limit)', 'feature_absent')
rule(r'^src/seeder/', 'excluded', 'DNS seeder: DNS name parsing/writing and seeder p2p handshake and address messages')

# --- wallet ----------------------------------------------------------------------------------------------------------------
rule(r'^src/wallet/', 'excluded', 'wallet: coin selection, wallet database and encryption, PSBT updater, wallet directory initialization, rescan, input size estimation')

# --- util data ---------------------------------------------------------------------------------------------------------------
rule(r'^test/util/data/', 'excluded', 'vectors of the bitcoin-tx / bitcoin-util command line tools (create, modify and sign transactions in JSON/hex): tool output, no node rule')

# --- functional: VM and signatures -------------------------------------------------------------------------------------------
rule(r'^test/functional/(abc-minimaldata|abc-schnorr|abc-schnorrmultisig|feature_schnorr_multisig|feature_anyonecanpay|feature_cltv|feature_dersig|'
     r'feature_sighash_utxos|feature_assumevalid|feature_int64_cscriptnum|feature_loops_functions_bitwise|feature_native_introspection)\.py$',
     'excluded', 'script interpreter or signature semantics: MINIMALDATA, Schnorr and multisig signatures, ANYONECANPAY, CLTV, DER signatures, SIGHASH_UTXOS, '
     'skipping signature checks under assumevalid, 64-bit script integers, loops/functions/bitwise opcodes, native introspection opcodes')
rule(r'^test/functional/abc-segwit-recovery\.py$', 'unsupported_feature',
     'relay policy script flags: a segwit recovery spend is accepted in blocks but must be refused by a standard-flags node (cleanstack / eval-false reasons, no ban). '
     'Knuth defines script_flags::standard_policy_flags (with bch_disallow_segwit_recovery) but nothing applies them to relay; the consensus acceptance in blocks is script semantics (VM)',
     'standardness_policy_absent')
rule(r'^test/functional/wallet_p2sh32\.py$', 'unsupported_feature',
     'P2SH32 standardness before activation: the transaction paying to a P2SH32 address is refused with scriptpubkey (code 64) and accepted after activation; '
     'the activation phase gating of relay standardness is absent (script::output_pattern also ignores the bch_p2sh_32 flag, see script_standard_Solver_success)',
     'standardness_policy_absent')

# --- functional: RPC, wallet, interfaces ------------------------------------------------------------------------------------
rule(r'^test/functional/(wallet_[a-z0-9_]+|bchn-feature-includewatching-fee|bchn-feature-preset-includewatching-fee|bchn-feature-preset-input-fee|'
     r'rpc_fundrawtransaction|rpc_psbt|rpc_signrawtransaction|rpc_createmultisig|rpc_decodescript|rpc_signmessage|rpc_scantxoutset|tool_wallet|'
     r'ui-importwallet-progress|bchn-rpc-tokens)\.py$', 'excluded',
     'wallet and wallet-driven RPC: address types, balances, keypool, HD, encryption, backup, labels, multiwallet, abandon/conflict accounting, fundrawtransaction fee sizing, '
     'PSBT, signing, token RPC output. The files were searched for node acceptance or rejection assertions (reject reasons, standardness, dust, relay fee): the only ones are wallet_p2sh32 '
     '(P2SH32 relay standardness) and wallet_abandonconflict (-minrelaytxfee), which have their own entries; the "Insufficient funds" and fee-estimation errors are wallet errors, not node rules')
rule(r'^test/functional/(interface_[a-z_]+|rpc_(bind|blockchain|deprecated|estimatefee|getblockstats|getchaintips|help|misc|named_arguments|net|rawtransaction|txoutproof|uptime|users|whitelist|listbanned|setban)|'
     r'feature_(help|config_args|includeconf|logging|uacomment|notifications|blocksdir|shutdown|proxy|coinstatsindex)|bchn-feature-(httptrace|indexdir|checkblockreads)|'
     r'bchn-rpc-(pipelining|node-sync|outdated-warnings|inactive-chain|getblockchaininfo-upgrade-status|gbtl-bg-cleaner|getblocktemplatelight|gbt-checkvalidity-ignorecache)|'
     r'mining_getblocktemplate_errors|mining_getblocktemplate_longpoll|abc-rpc-mocktime|create_cache|example_test|ui-example-test)\.py$',
     'excluded', 'RPC, REST, ZMQ and command-line interfaces, configuration and logging options, process lifecycle and getblocktemplate/getblocktemplatelight protocol details: '
     'response formats, options and plumbing. Block template validity (-gbtcheckvalidity) checks the same block rules ported under feature_block; getblockchaininfo upgrade_status and '
     'getchaintips report state computed by the rules compared in the activation ports')
rule(r'^test/functional/(feature_abortnode|feature_dbcrash|feature_reindex|feature_pruning|abc-sync-chain)\.py$', 'excluded',
     'node lifecycle and storage: abort on a failed disconnect, recovery after a crash during chainstate writes, -reindex, pruning, and leaving initial block download with out-of-order blocks; '
     'Knuth has its own restart/crash tests (transition_lifecycle, connected_tip, reorg_cycle)')
rule(r'^test/functional/feature_minchainwork\.py$', 'unsupported_feature',
     'nMinimumChainWork: a node does not leave initial block download nor process blocks of a chain whose work is below the configured minimum. Knuth has no minimum chain work setting', 'feature_absent')
rule(r'^test/functional/rpc_orphans\.py$', 'unsupported_feature', 'orphan transaction pool RPCs (getorphantxs-style listing and eviction): Knuth has no orphan pool', 'feature_absent')
rule(r'^test/functional/(mempool_fee_delta|mining_prioritisetransaction)\.py$', 'unsupported_feature', 'prioritisetransaction fee deltas: Knuth has no fee deltas', 'feature_absent')
rule(r'^test/functional/rpc_estimatefee\.py$', 'excluded', 'estimatefee/estimaterawfee argument handling and statistical estimator output')
rule(r'^test/functional/feature_(maxuploadtarget|peerratelimiter)\.py$', 'excluded', 'bandwidth and traffic accounting of served peers (-maxuploadtarget, per-peer rate limits): connection resource policy')

# --- functional: p2p -------------------------------------------------------------------------------------------------------------
rule(r'^test/functional/abc_p2p_version_timestamp\.py$', 'unsupported_feature',
     'network-adjusted time from the timestamp of VERSION messages (offset limits, median adjustment): Knuth has no peer time adjustment (see timedata_tests)', 'feature_absent')
rule(r'^test/functional/abc-p2p-compactblocks\.py$', 'excluded',
     'acceptance of bigger blocks (1 MB and above, canonical order) delivered as compact blocks: the block size and ordering rules are ported under abc-p2p-fullblocktest and feature_block; '
     'what this file adds is the compact block delivery path (BIP152), a relay message')
rule(r'^test/functional/(p2p_compactblocks|p2p_filter|p2p_mempool|p2p_blocksonly|p2p_sendheaders|p2p_invalid_locator|p2p_fingerprint|p2p_node_network_limited|p2p_getaddr_caching|'
     r'p2p_addr_relay|p2p_addrv2_advertise|p2p_addrv2_relay|p2p_add_connections|p2p_disconnect_ban|p2p_leak|p2p_leak_tx|p2p_extversion|p2p_extversion_disabled|p2p_extversion_no_misbehaving|p2p_onion_port|p2p_permissions|p2p_timeouts|p2p_tx_download|'
     r'p2p_invalid_messages|abc-invalid-message|abc-get-invalid-block|feature_asmap|rpc_net|bchn-feature-rejectsubversion|bchn-feature-txbroadcastinterval|bchn-txbroadcastinterval)\.py$',
     'excluded', 'peer protocol behavior: BIP152 compact block negotiation and reconstruction, BIP37 filters, the mempool message and blocks-only mode, headers announcement, locators, '
     'fingerprinting protections, extversion handshake (enabled, disabled, no misbehavior score), address relay, connection/ban/permission handling, message framing (magic, checksum, command, oversized payload), transaction download scheduling and '
     'inv batching, request of invalid blocks; each verifies how a peer is served or disconnected, none a validity rule (p2p_tx_download and p2p_leak_tx concern when transactions are requested and announced)')
rule(r'^test/functional/p2p_unrequested_blocks\.py$', 'unsupported_feature',
     'acceptance policy for blocks nobody requested: a low-work unrequested block is only stored as header (step 3), a more-work one is processed (step 4), blocks 288 ahead are not processed, '
     'duplicates are ignored, and nMinimumChainWork skips processing (step 2). Knuth syncs headers first and requests bodies, and has no unsolicited block handling or minimum chain work', 'feature_absent')
rule(r'^test/functional/(rpc_invalidateblock|rpc_preciousblock)\.py$', 'unsupported_feature',
     'operator commands that change which chain is followed: invalidateblock/reconsiderblock (candidate set repopulated after invalidation, -noparkdeepreorg) and preciousblock (preference between equal-work tips). '
     'Knuth has neither command and no way to mark a block invalid by hand', 'feature_absent')
rule(r'^test/functional/p2p_feefilter\.py$', 'unsupported_feature',
     'feefilter relay: a peer that sets a minimum fee rate (BIP133) is not sent invs of transactions below it. Knuth only sends a fixed feefilter (p2p_node.cpp) and does not apply the filter of its peers', 'feature_absent')
rule(r'^test/functional/(bchn-feature-doublespend-proof|bchn-rpc-dsproof|p2p_dsproof_getdata|ui-bchn-rpc-dsproof)\.py$', 'excluded',
     'double-spend proof relay extension (creation, relay, RPC and getdata batching): optional protocol whose validity is neither consensus nor standardness; Knuth only carries the message type')
rule(r'^test/functional/bchn-rpc-fillmempool\.py$', 'excluded', 'fillmempool test RPC that creates transactions to fill the mempool')
