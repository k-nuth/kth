// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/transaction_unconfirmed_entry.hpp>

#include <cstddef>
#include <cstdint>

// #include <kth/infrastructure.hpp>

namespace kth::database {

transaction_unconfirmed_entry::transaction_unconfirmed_entry(domain::chain::transaction const& tx, uint32_t arrival_time, uint32_t height)
    : transaction_(tx), arrival_time_(arrival_time), height_(height)
{}

domain::chain::transaction const& transaction_unconfirmed_entry::transaction() const {
    return transaction_;
}

uint32_t transaction_unconfirmed_entry::arrival_time() const {
    return arrival_time_;
}

uint32_t transaction_unconfirmed_entry::height() const {
    return height_;
}

// private
void transaction_unconfirmed_entry::reset() {
    transaction_ = domain::chain::transaction::null();
    arrival_time_ = max_uint32;
    height_ = max_uint32;
}

// Empty scripts are valid, validation relies on not_found only.
bool transaction_unconfirmed_entry::is_valid() const {
    // A transaction is always syntactically valid, so only this entry's own
    // sentinels are left to check.
    return arrival_time_ != kth::max_uint32 && height_ != kth::max_uint32;
}

// Size.
//-----------------------------------------------------------------------------
// constexpr
//TODO(fernando): make this constexpr
size_t transaction_unconfirmed_entry::serialized_size(domain::chain::transaction const& tx) {
    return tx.serialized_size(false)
         + sizeof(uint32_t) // arrival_time
         + sizeof(uint32_t); //height
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<transaction_unconfirmed_entry> transaction_unconfirmed_entry::from_data(byte_reader& reader) {
    auto tx = domain::chain::transaction::from_data(reader, false);
    if ( ! tx) {
        return std::unexpected(tx.error());
    }

    auto arrival_time = reader.read_little_endian<uint32_t>();
    if ( ! arrival_time) {
        return std::unexpected(arrival_time.error());
    }

    auto height = reader.read_little_endian<uint32_t>();
    if ( ! height) {
        return std::unexpected(height.error());
    }

    return transaction_unconfirmed_entry(std::move(*tx), *arrival_time, *height);
}

// Serialization.
//-----------------------------------------------------------------------------

// static
data_chunk transaction_unconfirmed_entry::factory_to_data(domain::chain::transaction const& tx, uint32_t arrival_time, uint32_t height) {
    auto const size = serialized_size(tx);
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = factory_to_data(writer, tx, arrival_time, height);
    KTH_ASSERT(r.has_value());
    return data;
}

data_chunk transaction_unconfirmed_entry::to_data() const {
    return kth::to_data_chunk(*this);
}

} // namespace kth::database
