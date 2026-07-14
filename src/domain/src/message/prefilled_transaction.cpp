// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/prefilled_transaction.hpp>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
namespace kth::domain::message {

#if defined(KTH_CURRENCY_BCH)
constexpr size_t max_index = max_uint32;
#else
constexpr size_t max_index = max_uint16;
#endif

prefilled_transaction::prefilled_transaction(uint64_t index, chain::transaction tx)
    : index_(index), transaction_(std::move(tx)) {
}

// static
expect<prefilled_transaction> prefilled_transaction::create(uint64_t index, chain::transaction tx) {
    // BIP152 caps the prefilled index; anything above it cannot be encoded.
    if (index >= max_index) {
        return std::unexpected(error::invalid_compact_block);
    }
    return prefilled_transaction(index, std::move(tx));
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<prefilled_transaction> prefilled_transaction::from_data(byte_reader& reader, uint32_t version) {
    auto const index = reader.read_variable_little_endian();
    if ( ! index) {
        return std::unexpected(index.error());
    }
    auto const transaction = chain::transaction::from_data(reader, true);
    if ( ! transaction) {
        return std::unexpected(transaction.error());
    }
    return create(*index, std::move(*transaction));
}

// Serialization.
//-----------------------------------------------------------------------------



size_t prefilled_transaction::serialized_size(uint32_t /*version*/) const {
    return infrastructure::message::variable_uint_size(index_) +
           transaction_.serialized_size(true);
}

uint64_t prefilled_transaction::index() const {
    return index_;
}

chain::transaction const& prefilled_transaction::transaction() const {
    return transaction_;
}

expect<void> prefilled_transaction::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_variable_little_endian(index_); ! r) return r;
        transaction_.to_data(writer, true);
        return {};
}

} // namespace kth::domain::message
