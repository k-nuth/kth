// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/query.hpp>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>

#include <kth/blockchain/interface/block_chain.hpp>
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

namespace {

// 256-bit value as a 64-char, zero-padded, big-endian hex string (chainwork).
std::string to_hex256(uint256_t const& value) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(64) << value;
    return os.str();
}

} // namespace

std::string_view chain_name(domain::config::network net) {
    using domain::config::network;
    switch (net) {
        case network::testnet:
            return "test";
        case network::regtest:
            return "regtest";
#if defined(KTH_CURRENCY_BCH)
        case network::testnet4:
            return "test4";
        case network::scalenet:
            return "scale";
        case network::chipnet:
            return "chip";
#endif
        default:
        case network::mainnet:
            return "main";
    }
}

std::string render_blockchain_info(blockchain_info const& info) {
    // By height: kth keeps no per-block transaction counts to weigh it by.
    auto const progress = info.headers == 0 ? 1.0
        : std::min(1.0, static_cast<double>(info.blocks) / static_cast<double>(info.headers));

    writer w;
    w.begin_object();
    w.field("chain", info.chain);
    w.field("blocks", static_cast<std::uint64_t>(info.blocks));
    w.field("headers", static_cast<std::uint64_t>(info.headers));
    w.field("bestblockhash", encode_hash(info.best_block_hash));
    w.field("difficulty", info.difficulty);
    w.field("mediantime", static_cast<std::int64_t>(info.median_time));
    w.field("verificationprogress", progress);
    w.field("initialblockdownload", info.initial_block_download);
    if (info.chain_work) {
        w.field("chainwork", to_hex256(*info.chain_work));
    }
    w.field("size_on_disk", info.size_on_disk);
    w.field("warnings", "");
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
