// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// A minimal client for the Cap'n Proto RPC backend (rpc=capnproto).
//
//   kth_capnp_chain_client <port> <user> <password> chain-info
//   kth_capnp_chain_client <port> <user> <password> block <hash>
//   kth_capnp_chain_client <port> <user> <password> tx <hash>
//
// <hash> is the usual displayed hex (what explorers and getblockhash print); it
// is reversed here into the schema's raw byte order. Connects to 127.0.0.1.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>

#include <kth/rpc/v1/chain.capnp.h>

namespace v1 = kth::rpc::v1;

namespace {

std::string hex(kj::ArrayPtr<kj::byte const> data, bool reverse) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < data.size(); ++i) {
        auto const byte = data[reverse ? data.size() - 1 - i : i];
        out += digits[byte >> 4];
        out += digits[byte & 0xf];
    }
    return out;
}

// Displayed hex -> 32 raw bytes in schema (internal) order.
bool parse_displayed_hash(std::string const& text, std::vector<kj::byte>& out) {
    if (text.size() != 64) return false;
    out.clear();
    for (std::size_t i = 0; i < 64; i += 2) {
        try {
            out.push_back(static_cast<kj::byte>(std::stoi(text.substr(i, 2), nullptr, 16)));
        } catch (...) {
            return false;
        }
    }
    std::reverse(out.begin(), out.end());
    return true;
}

char const* name(v1::ErrorCode code) {
    switch (code) {
        case v1::ErrorCode::INVALID_ARGUMENT:   return "invalidArgument";
        case v1::ErrorCode::NOT_FOUND:          return "notFound";
        case v1::ErrorCode::RESOURCE_EXHAUSTED: return "resourceExhausted";
        case v1::ErrorCode::UNAVAILABLE:        return "unavailable";
        case v1::ErrorCode::UNAUTHENTICATED:    return "unauthenticated";
        default:                                return "internal";
    }
}

int report(v1::Error::Reader error) {
    std::fprintf(stderr, "error %s: %s\n", name(error.getCode()), error.getMessage().cStr());
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <port> <user> <password> chain-info|block <hash>|tx <hash>\n", argv[0]);
        return 2;
    }
    std::string const command = argv[4];
    std::vector<kj::byte> hash;
    if (command != "chain-info" && (argc < 6 || ! parse_displayed_hash(argv[5], hash))) {
        std::fprintf(stderr, "a 64-digit hex hash is required\n");
        return 2;
    }

    try {
        auto io = kj::setupAsyncIo();
        auto& scope = io.waitScope;
        auto address = io.provider->getNetwork()
            .parseAddress("127.0.0.1", static_cast<std::uint16_t>(std::stoi(argv[1]))).wait(scope);
        auto stream = address->connect().wait(scope);
        capnp::TwoPartyClient client(*stream);
        auto gateway = client.bootstrap().castAs<v1::Gateway>();

        auto login = gateway.loginRequest();
        login.setUser(argv[2]);
        login.setPassword(argv[3]);
        auto session = login.send().wait(scope);
        if (session.getResult().isError()) return report(session.getResult().getError());
        auto chain = session.getResult().getChain();

        if (command == "chain-info") {
            auto response = chain.getChainInfoRequest().send().wait(scope);
            if (response.getResult().isError()) return report(response.getResult().getError());
            auto info = response.getResult().getOk();
            std::printf("block height:  %llu\nheader height: %llu\nbest block:    %s\n",
                static_cast<unsigned long long>(info.getBlockHeight()),
                static_cast<unsigned long long>(info.getHeaderHeight()),
                hex(info.getBestBlockHash(), /*reverse*/ true).c_str());
        } else if (command == "block") {
            auto request = chain.getBlockRequest();
            request.setHash(kj::arrayPtr(hash.data(), hash.size()));
            auto response = request.send().wait(scope);
            if (response.getResult().isError()) return report(response.getResult().getError());
            auto block = response.getResult().getOk();
            std::printf("height: %llu\nwire:   %zu bytes\n",
                static_cast<unsigned long long>(block.getHeight()), block.getWire().size());
        } else if (command == "tx") {
            auto request = chain.getTransactionRequest();
            request.setHash(kj::arrayPtr(hash.data(), hash.size()));
            auto response = request.send().wait(scope);
            if (response.getResult().isError()) return report(response.getResult().getError());
            auto tx = response.getResult().getOk();
            std::printf("wire:   %zu bytes\nstatus: %s\n", tx.getWire().size(),
                tx.getLocation().isMempool() ? "mempool" : "confirmed");
        } else {
            std::fprintf(stderr, "unknown command '%s'\n", command.c_str());
            return 2;
        }
    } catch (kj::Exception const& e) {
        std::fprintf(stderr, "rpc failed: %s\n", e.getDescription().cStr());
        return 1;
    }
    return 0;
}
