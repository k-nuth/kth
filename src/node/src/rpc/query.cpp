// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/query.hpp>

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
    auto const size = tx.serialized_size(/*wire*/ true);
    data_chunk buffer(size);
    byte_writer writer(std::span<uint8_t>{buffer.data(), size});
    tx.to_data(writer, /*wire*/ true);
    return encode_base16(buffer);
}

std::string block_to_hex(domain::chain::block const& block) {
    auto const size = block.serialized_size();
    data_chunk buffer(size);
    byte_writer writer(std::span<uint8_t>{buffer.data(), size});
    block.to_data(writer);
    return encode_base16(buffer);
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
