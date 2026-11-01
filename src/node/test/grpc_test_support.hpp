// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_TEST_GRPC_TEST_SUPPORT_HPP
#define KTH_NODE_TEST_GRPC_TEST_SUPPORT_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <grpcpp/grpcpp.h>

#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/node/grpc_rpc/transport.hpp>
#include <kth/rpc/v1/node.grpc.pb.h>

// A real generated gRPC client for the loopback tests: nothing here is a double of
// the transport, only of what sits behind it.

namespace kth::node::test {

namespace v1 = ::kth::rpc::v1;

inline std::string basic_auth(std::string const& user, std::string const& password) {
    auto const pair = user + ":" + password;
    return "Basic " + encode_base64(byte_span(
        reinterpret_cast<uint8_t const*>(pair.data()), pair.size()));
}

template <typename Response>
struct reply {
    grpc::Status status;
    Response response;
};

class client {
public:
    explicit client(std::uint16_t port, std::string authorization = basic_auth("user", "secret"))
        : stub_(v1::NodeService::NewStub(grpc::CreateChannel(
            "127.0.0.1:" + std::to_string(port), grpc::InsecureChannelCredentials())))
        , authorization_(std::move(authorization))
    {}

    // `cancel` receives the context so a test can TryCancel it from another thread.
    template <typename Response, typename Request, typename Method>
    reply<Response> call(Method method, Request const& request,
        std::function<void(grpc::ClientContext&)> const& configure = {}) const
    {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(20));
        if ( ! authorization_.empty()) {
            context.AddMetadata("authorization", authorization_);
        }
        if (configure) {
            configure(context);
        }
        reply<Response> out;
        out.status = (stub_.get()->*method)(&context, request, &out.response);
        return out;
    }

    reply<v1::GetChainInfoResponse> chain_info() const {
        return call<v1::GetChainInfoResponse>(&v1::NodeService::Stub::GetChainInfo,
            v1::GetChainInfoRequest{});
    }

    reply<v1::GetBlockResponse> block(std::string const& hash) const {
        v1::GetBlockRequest request;
        request.set_hash(hash);
        return call<v1::GetBlockResponse>(&v1::NodeService::Stub::GetBlock, request);
    }

    reply<v1::GetTransactionResponse> transaction(std::string const& hash) const {
        v1::GetTransactionRequest request;
        request.set_hash(hash);
        return call<v1::GetTransactionResponse>(&v1::NodeService::Stub::GetTransaction, request);
    }

    v1::NodeService::Stub& stub() const { return *stub_; }

private:
    std::unique_ptr<v1::NodeService::Stub> stub_;
    std::string authorization_;
};

} // namespace kth::node::test

#endif
