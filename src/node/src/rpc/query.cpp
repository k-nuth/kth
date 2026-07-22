// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/query.hpp>

#include <cstdint>
#include <span>

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

} // namespace kth::node::rpc
