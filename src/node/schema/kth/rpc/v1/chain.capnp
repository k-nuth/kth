# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

@0xd810e12b35003eae;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("kth::rpc::v1");

# Read-only chain API, version 1.
#
# This is Cap'n Proto RPC: clients obtain the bootstrap `Gateway`, call `login`
# with the node's rpc.user/rpc.password (or the generated cookie), and use the
# returned `Chain` capability. Nothing else is reachable before a login.
#
# The schema is append-only within v1: ordinals and field meanings never change.

using Hash = Data;
# Exactly 32 bytes in the node's internal ("natural") byte order, which is the
# order the bytes have inside serialized headers, transactions and blocks. It is
# the REVERSE of the hexadecimal strings that explorers and the JSON-RPC backend
# print. To convert a displayed txid/block hash, decode the hex and reverse the
# bytes. A Hash of any other length is rejected with `invalidArgument`.

enum ErrorCode {
  invalidArgument @0;
  # The request was malformed (for example a hash that is not 32 bytes). Retrying
  # the same request cannot succeed.
  notFound @1;
  # The object is unknown to this node, or is not available for this lookup.
  resourceExhausted @2;
  # A node limit was hit: too many calls in flight, or a result larger than the
  # response limit. Nothing was done for an overload; retry later.
  unavailable @3;
  # The node is shutting down, the call was cancelled, or the chain is stopped.
  unauthenticated @4;
  # The credentials were wrong.
  internal @5;
  # The node failed in a way that is not the caller's fault.
}

struct Error {
  code @0 :ErrorCode;
  message @1 :Text;
  # For humans. Not a stable contract: branch on `code`.
}

struct ChainInfo {
  blockHeight @0 :UInt64;
  # Height of the most-work fully validated block (the connected tip).
  headerHeight @1 :UInt64;
  # Height of the best header known; never below blockHeight.
  bestBlockHash @2 :Hash;
  # Hash of the connected tip block at blockHeight.
}

struct Block {
  hash @0 :Hash;
  # Echo of the requested hash.
  height @1 :UInt64;
  wire @2 :Data;
  # The block exactly as stored and relayed on the peer-to-peer network
  # (header, transaction count, transactions). Not hexadecimal.
}

struct Transaction {
  hash @0 :Hash;
  # Echo of the requested hash.
  wire @1 :Data;
  # The transaction in its peer-to-peer wire serialization. Not hexadecimal.
  location :union {
    mempool @2 :Void;
    confirmed :group {
      blockHeight @3 :UInt64;
      index @4 :UInt64;
      # Position within the block.
    }
  }
}

# Application errors travel as data in these results. Cap'n Proto exceptions
# (DISCONNECTED, OVERLOADED, ...) are reserved for the transport: a closed
# connection, a message above the size limit, a protocol violation.

struct ChainInfoResult {
  union {
    ok @0 :ChainInfo;
    error @1 :Error;
  }
}

struct BlockResult {
  union {
    ok @0 :Block;
    error @1 :Error;
  }
}

struct TransactionResult {
  union {
    ok @0 :Transaction;
    error @1 :Error;
  }
}

interface Chain $Cxx.allowCancellation {
  # Dropping a call, or closing the connection, cancels the node-side work.
  getChainInfo @0 () -> (result :ChainInfoResult);

  getBlock @1 (hash :Hash) -> (result :BlockResult);
  # Only blocks on the active chain, at or below the connected tip.

  getTransaction @2 (hash :Hash) -> (result :TransactionResult);
  # Currently answers from the mempool; confirmed lookup by hash is reported as
  # notFound until the node has a transaction index (see docs/capnproto-rpc.md).
}

struct LoginResult {
  union {
    chain @0 :Chain;
    error @1 :Error;
  }
}

interface Gateway {
  # The bootstrap capability.
  login @0 (user :Text, password :Text) -> (result :LoginResult);
  # Three wrong attempts, or no successful login within the node's login
  # timeout, close the connection.
}
