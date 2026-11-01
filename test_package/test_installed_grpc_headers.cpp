// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compile-only package consumer for rpc=grpc: the installed headers, including the
// generated kth/rpc/v1 messages, must be enough on their own.
#include <kth/node/grpc_rpc/chain_handlers.hpp>
#include <kth/node/grpc_rpc/server.hpp>
#include <kth/node/grpc_rpc/transport.hpp>
#include <kth/rpc/v1/node.grpc.pb.h>
#include <kth/rpc/v1/node.pb.h>

#if ! defined(KTH_WITH_GRPC)
#error "the installed node component must define KTH_WITH_GRPC for rpc=grpc"
#endif

int grpc_consumer_probe() {
    kth::node::grpc_rpc::transport_options options;
    kth::rpc::v1::GetBlockRequest request;
    request.set_hash(std::string(32, '\0'));
    return static_cast<int>(request.hash().size() + options.max_in_flight);
}
