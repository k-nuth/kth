// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Smoke client for a node built with rpc=grpc (see docs/grpc.md).
//
//   kth-grpc-smoke-client [--target 127.0.0.1:8332] [--cookie .cookie | --user U --password P]
//                         [--block <hash>] [--tx <hash>]
//
// Hashes are given as the usual displayed hex (as block explorers print them);
// the client reverses them into the API's internal byte order before sending.
// Exit status: 0 every call succeeded, 1 a call failed, 2 bad usage.

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <grpcpp/grpcpp.h>

#include <kth/rpc/v1/node.grpc.pb.h>

namespace v1 = ::kth::rpc::v1;

namespace {

std::string base64(std::string const& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < input.size(); i += 3) {
        auto const n = (static_cast<unsigned char>(input[i]) << 16) |
            (static_cast<unsigned char>(input[i + 1]) << 8) | static_cast<unsigned char>(input[i + 2]);
        out += alphabet[(n >> 18) & 63]; out += alphabet[(n >> 12) & 63];
        out += alphabet[(n >> 6) & 63]; out += alphabet[n & 63];
    }
    if (i + 1 == input.size()) {
        auto const n = static_cast<unsigned char>(input[i]) << 16;
        out += alphabet[(n >> 18) & 63]; out += alphabet[(n >> 12) & 63]; out += "==";
    } else if (i + 2 == input.size()) {
        auto const n = (static_cast<unsigned char>(input[i]) << 16) |
            (static_cast<unsigned char>(input[i + 1]) << 8);
        out += alphabet[(n >> 18) & 63]; out += alphabet[(n >> 12) & 63];
        out += alphabet[(n >> 6) & 63]; out += '=';
    }
    return out;
}

// Displayed hex -> the 32 internal-order bytes. Empty on malformed input.
std::string hash_from_display_hex(std::string const& hex) {
    if (hex.size() != 64) {
        return {};
    }
    std::string bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        try {
            std::size_t used = 0;
            auto const value = std::stoi(hex.substr(i, 2), &used, 16);
            if (used != 2) return {};
            bytes.push_back(static_cast<char>(value));
        } catch (...) {
            return {};
        }
    }
    std::reverse(bytes.begin(), bytes.end());
    return bytes;
}

std::string to_display_hex(std::string bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::reverse(bytes.begin(), bytes.end());
    std::string out;
    for (unsigned char byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

int fail(grpc::Status const& status, char const* what) {
    std::cerr << what << " failed: " << static_cast<int>(status.error_code()) << " "
              << status.error_message() << '\n';
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    std::string target = "127.0.0.1:8332";
    std::string cookie_path;
    std::string user;
    std::string password;
    std::string block_hash;
    std::string tx_hash;

    for (int i = 1; i < argc; ++i) {
        std::string const flag = argv[i];
        auto const value = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << flag << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (flag == "--target") target = value();
        else if (flag == "--cookie") cookie_path = value();
        else if (flag == "--user") user = value();
        else if (flag == "--password") password = value();
        else if (flag == "--block") block_hash = value();
        else if (flag == "--tx") tx_hash = value();
        else { std::cerr << "unknown option " << flag << '\n'; return 2; }
    }

    std::string credentials = user + ":" + password;
    if (user.empty() && password.empty()) {
        if (cookie_path.empty()) cookie_path = ".cookie";
        std::ifstream cookie(cookie_path);
        credentials.assign(std::istreambuf_iterator<char>(cookie), std::istreambuf_iterator<char>());
        if (credentials.empty()) {
            std::cerr << "no credentials: pass --user/--password or a readable --cookie file\n";
            return 2;
        }
    }

    auto stub = v1::NodeService::NewStub(
        grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    auto const context = [&] {
        auto context = std::make_unique<grpc::ClientContext>();
        context->AddMetadata("authorization", "Basic " + base64(credentials));
        context->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
        return context;
    };

    v1::GetChainInfoResponse info;
    if (auto status = stub->GetChainInfo(context().get(), v1::GetChainInfoRequest{}, &info); ! status.ok()) {
        return fail(status, "GetChainInfo");
    }
    std::cout << "network " << v1::Network_Name(info.network()) << '\n'
              << "block_height " << info.block_height() << '\n'
              << "header_height " << info.header_height() << '\n'
              << "best_block_hash " << to_display_hex(info.best_block_hash()) << '\n';

    if ( ! block_hash.empty()) {
        v1::GetBlockRequest request;
        request.set_hash(hash_from_display_hex(block_hash));
        v1::GetBlockResponse response;
        if (auto status = stub->GetBlock(context().get(), request, &response); ! status.ok()) {
            return fail(status, "GetBlock");
        }
        std::cout << "block height " << response.height() << " bytes " << response.raw_block().size() << '\n';
    }
    if ( ! tx_hash.empty()) {
        v1::GetTransactionRequest request;
        request.set_hash(hash_from_display_hex(tx_hash));
        v1::GetTransactionResponse response;
        if (auto status = stub->GetTransaction(context().get(), request, &response); ! status.ok()) {
            return fail(status, "GetTransaction");
        }
        std::cout << "transaction bytes " << response.raw_transaction().size()
                  << (response.has_confirmation() ? " confirmed" : " mempool") << '\n';
    }
    return 0;
}
