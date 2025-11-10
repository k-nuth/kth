// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/block_transactions.hpp>

// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const block_transactions::command = "blocktxn";
uint32_t const block_transactions::version_minimum = version::level::bip152;
uint32_t const block_transactions::version_maximum = version::level::bip152;

block_transactions::block_transactions()
    : block_hash_(null_hash)
{}

block_transactions::block_transactions(hash_digest const& block_hash, chain::transaction::list const& transactions)
    : block_hash_(block_hash)
    , transactions_(transactions)
{}

block_transactions::block_transactions(hash_digest const& block_hash, chain::transaction::list&& transactions)
    : block_hash_(block_hash)
    , transactions_(std::move(transactions))
{}

bool block_transactions::operator==(block_transactions const& x) const {
    return (block_hash_ == x.block_hash_) && (transactions_ == x.transactions_);
}

bool block_transactions::operator!=(block_transactions const& x) const {
    return !(*this == x);
}

bool block_transactions::is_valid() const {
    return (block_hash_ != null_hash);
}

void block_transactions::reset() {
    block_hash_ = null_hash;
    transactions_.clear();
    transactions_.shrink_to_fit();
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<block_transactions> block_transactions::from_data(byte_reader& reader, uint32_t version) {
    auto const block_hash = read_hash(reader);
    if ( ! block_hash) {
        return std::unexpected(block_hash.error());
    }

    auto txs = read_collection<chain::transaction>(reader, true);
    if ( ! txs) {
        return std::unexpected(txs.error());
    }

    if (version < block_transactions::version_minimum) {
        return std::unexpected(error::version_too_low);
    }

    return block_transactions(*block_hash, std::move(*txs));
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk block_transactions::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void block_transactions::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t block_transactions::serialized_size(uint32_t /*version*/) const {
    auto size = hash_size + infrastructure::message::variable_uint_size(transactions_.size());

    for (auto const& element : transactions_) {
        size += element.serialized_size(/*wire*/ true);
    }

    return size;
}

hash_digest& block_transactions::block_hash() {
    return block_hash_;
}

hash_digest const& block_transactions::block_hash() const {
    return block_hash_;
}

void block_transactions::set_block_hash(hash_digest const& value) {
    block_hash_ = value;
}

chain::transaction::list& block_transactions::transactions() {
    return transactions_;
}

chain::transaction::list const& block_transactions::transactions() const {
    return transactions_;
}

void block_transactions::set_transactions(chain::transaction::list const& x) {
    transactions_ = x;
}

void block_transactions::set_transactions(chain::transaction::list&& x) {
    transactions_ = std::move(x);
}


} // namespace kth::domain::message
