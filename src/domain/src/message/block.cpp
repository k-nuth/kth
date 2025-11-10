// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/block.hpp>

#include <cstddef>
#include <cstdint>
#include <istream>
#include <utility>

#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <kth/infrastructure/utility/reader.hpp>

namespace kth::domain::message {

std::string const block::command = "block";
uint32_t const block::version_minimum = version::level::minimum;
uint32_t const block::version_maximum = version::level::maximum;

block::block(chain::block&& x)
    : chain::block(std::move(x))
{}

block::block(chain::block const& x)
    : chain::block(x)
{}

block::block(chain::header const& header, chain::transaction::list&& transactions)
    : chain::block(header, std::move(transactions))
{}

block::block(chain::header const& header, chain::transaction::list const& transactions)
    : chain::block(header, transactions)
{}

// block::block(block&& x) noexcept
//     : chain::block(std::move(x))
// {}

block& block::operator=(chain::block&& x) {
    reset();
    chain::block::operator=(std::move(x));
    return *this;
}

// block& block::operator=(block&& x) noexcept {
//     chain::block::operator=(std::move(x));
//     return *this;
// }

bool block::operator==(chain::block const& x) const {
    return chain::block::operator==(x);
}

bool block::operator!=(chain::block const& x) const {
    return chain::block::operator!=(x);
}

bool block::operator==(block const& x) const {
    return chain::block::operator==(x);
}

bool block::operator!=(block const& x) const {
    return !(*this == x);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<block> block::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto chain_block = chain::block::from_data(reader);
    if ( ! chain_block) {
        return std::unexpected(chain_block.error());
    }
    return block(std::move(*chain_block));
}

// Serialization.
//-----------------------------------------------------------------------------

// Witness is always serialized if present.
// NOTE: Witness on BCH is dissabled on the chain::block class
data_chunk block::to_data(uint32_t /*unused*/) const {
    return chain::block::to_data(true);
}

void block::to_data(uint32_t /*version*/, data_sink& stream) const {
    chain::block::to_data(stream);
}

size_t block::serialized_size(uint32_t /*unused*/) const {
    return chain::block::serialized_size();
}

// //TODO(fernando): check this family of functions: to_data_header_nonce
// void to_data_header_nonce(block const& block, uint64_t nonce, writer& sink) {
//     block.header().to_data(sink);
//     sink.write_8_bytes_little_endian(nonce);
// }

// void to_data_header_nonce(block const& block, uint64_t nonce, std::ostream& stream) {
void to_data_header_nonce(block const& block, uint64_t nonce, data_sink& stream) {
    ostream_writer sink_w(stream);
    to_data_header_nonce(block, nonce, sink_w);
}

data_chunk to_data_header_nonce(block const& block, uint64_t nonce) {
    data_chunk data;
    auto size = chain::header::satoshi_fixed_size() + sizeof(nonce);

    data.reserve(size);
    data_sink ostream(data);
    to_data_header_nonce(block, nonce, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

hash_digest hash(block const& block, uint64_t nonce) {
    return sha256_hash(to_data_header_nonce(block, nonce));
}

} // namespace kth::domain::message
