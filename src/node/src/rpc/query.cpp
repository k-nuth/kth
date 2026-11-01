// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/query.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/rpc/json.hpp>

namespace kth::node::rpc {

std::string transaction_to_hex(domain::chain::transaction const& tx) {
    return encode_base16(to_data_chunk(tx, /*wire*/ true));
}

std::string block_to_hex(domain::chain::block const& block) {
    return encode_base16(to_data_chunk(block));
}

std::string render_blockchain_info(
    std::string_view chain, std::size_t blocks, std::size_t headers,
    std::string_view best_block_hash, double difficulty) {

    writer w;
    w.begin_object();
    w.field("chain", chain);
    w.field("blocks", static_cast<std::uint64_t>(blocks));
    w.field("headers", static_cast<std::uint64_t>(headers));
    w.field("bestblockhash", best_block_hash);
    w.field("difficulty", difficulty);
    w.end_object();
    return w.str();
}

namespace {

// Compact nBits as an 8-char hex string (the getblockheader bits format).
std::string bits_to_hex(std::uint32_t bits) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(8) << bits;
    return os.str();
}

void write_hash_array(writer& w, hash_list const& hashes) {
    w.begin_array();
    for (auto const& h : hashes) {
        w.value(encode_hash(h));
    }
    w.end_array();
}

} // namespace

std::string render_hash_list(hash_list const& hashes) {
    writer w;
    write_hash_array(w, hashes);
    return w.str();
}

std::string render_mempool_info(blockchain::mempool_totals const& totals) {
    writer w;
    w.begin_object();
    w.field("size", static_cast<std::uint64_t>(totals.size));
    w.field("bytes", static_cast<std::uint64_t>(totals.bytes));
    w.field("total_fee", static_cast<std::uint64_t>(totals.total_fee));
    w.end_object();
    return w.str();
}

std::string render_mempool_entry(blockchain::mempool_entry_info const& entry,
                                 hash_list const& depends, hash_list const& spentby) {
    writer w;
    w.begin_object();
    w.field("fee", static_cast<std::uint64_t>(entry.fee));
    w.field("size", static_cast<std::uint64_t>(entry.size));
    w.field("time", static_cast<std::uint64_t>(entry.time));
    w.key("depends");
    write_hash_array(w, depends);
    w.key("spentby");
    write_hash_array(w, spentby);
    w.end_object();
    return w.str();
}

namespace {

// "1.5.2" -> 1050200, the integer clients compare node versions with. A part
// that is missing or not a number counts as 0.
std::uint64_t version_number(std::string_view version) {
    std::uint64_t number = 0;
    for (std::uint64_t const scale : {1000000u, 10000u, 100u}) {
        std::uint64_t part = 0;
        auto const [end, ec] = std::from_chars(version.data(), version.data() + version.size(), part);
        if (ec != std::errc{}) {
            break;
        }
        number += part * scale;
        version.remove_prefix(std::size_t(end - version.data()));
        if (version.empty() || version.front() != '.') {
            break;
        }
        version.remove_prefix(1);
    }
    return number;
}

// The names of the service bits in `services`, by bit order.
std::vector<std::string> service_names(std::uint64_t services) {
    using service = domain::message::version::service;
    std::vector<std::string> names;
    for (unsigned bit = 0; bit < 64; ++bit) {
        auto const flag = std::uint64_t(1) << bit;
        if ((services & flag) == 0) {
            continue;
        }
        switch (flag) {
            case service::node_network: names.emplace_back("NETWORK"); break;
            case service::node_getutxo: names.emplace_back("GETUTXO"); break;
            case service::node_bloom: names.emplace_back("BLOOM"); break;
#if defined(KTH_CURRENCY_BCH)
            case service::node_xthin: names.emplace_back("XTHIN"); break;
            case service::node_bitcoin_cash: names.emplace_back("BITCOIN_CASH"); break;
            case service::node_graphene: names.emplace_back("GRAPHENE"); break;
            case service::node_compact_filters: names.emplace_back("CF"); break;
            case service::node_extversion: names.emplace_back("EXTVERSION"); break;
#endif
            case service::node_network_limited: names.emplace_back("NETWORK_LIMITED"); break;
            default: names.push_back("UNKNOWN[2^" + std::to_string(bit) + "]"); break;
        }
    }
    return names;
}

} // namespace

std::string render_network_info(network_info const& info) {
    constexpr double satoshis_per_bch = 100'000'000.0;
    auto const relay_fee_per_kb = std::llround(static_cast<double>(info.byte_fee_satoshis) * 1000.0);

    std::ostringstream services;
    services << std::hex << std::setfill('0') << std::setw(16) << info.services;

    writer w;
    w.begin_object();
    w.field("version", version_number(info.version));
    w.field("subversion", info.subversion);
    w.field("protocolversion", static_cast<std::uint64_t>(info.protocol_version));
    w.field("localservices", services.str());
    w.key("localservicesnames").begin_array();
    for (auto const& name : service_names(info.services)) {
        w.value(name);
    }
    w.end_array();
    w.field("localrelay", info.relay);
    w.field("networkactive", true);
    w.field("connections", static_cast<std::uint64_t>(info.connections));
    w.field("relayfee", static_cast<double>(std::max(relay_fee_per_kb, 0LL)) / satoshis_per_bch);
    w.field("warnings", "");
    w.end_object();
    return w.str();
}

std::string header_to_hex(domain::chain::header const& header) {
    return encode_base16(to_data_chunk(header, /*wire*/ true));
}

std::string render_block_header(domain::chain::header const& header,
                                std::size_t height, hash_digest const& hash) {
    writer w;
    w.begin_object();
    w.field("hash", encode_hash(hash));
    w.field("height", static_cast<std::uint64_t>(height));
    w.field("version", static_cast<std::int64_t>(header.version()));
    w.field("previousblockhash", encode_hash(header.previous_block_hash()));
    w.field("merkleroot", encode_hash(header.merkle()));
    w.field("time", static_cast<std::int64_t>(header.timestamp()));
    w.field("bits", bits_to_hex(header.bits()));
    w.field("nonce", static_cast<std::int64_t>(header.nonce()));
    w.end_object();
    return w.str();
}

} // namespace kth::node::rpc
