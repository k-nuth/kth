// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/transaction_unconfirmed_entry.hpp>

#include <cstddef>
#include <cstdint>

// #include <kth/infrastructure.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

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
    transaction_ = domain::chain::transaction{};
    arrival_time_ = max_uint32;
    height_ = max_uint32;
}

// Empty scripts are valid, validation relies on not_found only.
bool transaction_unconfirmed_entry::is_valid() const {
    return transaction_.is_valid() && arrival_time_ != kth::max_uint32  && height_ != kth::max_uint32;
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
    data_chunk data;
    auto const size = serialized_size(tx);
    data.reserve(size);
    data_sink ostream(data);
    factory_to_data(ostream, tx, arrival_time, height);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

// static
void transaction_unconfirmed_entry::factory_to_data(std::ostream& stream, domain::chain::transaction const& tx, uint32_t arrival_time, uint32_t height) {
    ostream_writer sink(stream);
    factory_to_data(sink, tx, arrival_time, height);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk transaction_unconfirmed_entry::to_data() const {
    data_chunk data;
    auto const size = serialized_size(transaction_);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void transaction_unconfirmed_entry::to_data(std::ostream& stream) const {
    ostream_writer sink(stream);
    to_data(sink);
}

} // namespace kth::database
