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
#include <kth/infrastructure/utility/data.hpp>
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


size_t block::serialized_size(uint32_t /*unused*/) const {
    return chain::block::serialized_size();
}


hash_digest hash(block const& block, uint64_t nonce) {
    auto const size = chain::header::satoshi_fixed_size() + sizeof(nonce);
    data_chunk buf(size);
    byte_writer writer(buf);
    auto const r1 = block.header().to_data(writer, true);
    auto const r2 = writer.write_little_endian<uint64_t>(nonce);
    KTH_ASSERT(r1.has_value() && r2.has_value());
    return sha256_hash(buf);
}

expect<void> block::to_data(byte_writer& writer, uint32_t version) const {
        chain::block::to_data(writer);
        return {};
}

} // namespace kth::domain::message
