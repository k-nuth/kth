// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/databases/utxo_entry.hpp>

#include <cstddef>
#include <cstdint>

// #include <kth/infrastructure.hpp>

namespace kth::database {

utxo_entry::utxo_entry(domain::chain::output output, uint32_t height, uint32_t median_time_past, bool coinbase)
    : output_(std::move(output)), height_(height), median_time_past_(median_time_past), coinbase_(coinbase)
{}

domain::chain::output const& utxo_entry::output() const {
    return output_;
}

uint32_t utxo_entry::height() const {
    return height_;
}

uint32_t utxo_entry::median_time_past() const {
    return median_time_past_;
}

bool utxo_entry::coinbase() const {
    return coinbase_;
}

// private
void utxo_entry::reset() {
    output_ = domain::chain::output{};
    height_ = max_uint32;
    median_time_past_ = max_uint32;
    coinbase_ = false;
}

// Empty scripts are valid, validation relies on not_found only.
bool utxo_entry::is_valid() const {
    return output_.is_valid() && height_ != kth::max_uint32 && median_time_past_ != max_uint32;
}


// Size.
//-----------------------------------------------------------------------------

// private static
constexpr
size_t utxo_entry::serialized_size_fixed() {
    return sizeof(uint32_t) + sizeof(uint32_t) + sizeof(bool);
}

size_t utxo_entry::serialized_size() const {
    return output_.serialized_size(false) + serialized_size_fixed();
}

// Serialization.
//-----------------------------------------------------------------------------

// static
data_chunk utxo_entry::to_data_fixed(uint32_t height, uint32_t median_time_past, bool coinbase) {
    auto const size = serialized_size_fixed();
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = to_data_fixed(writer, height, median_time_past, coinbase);
    KTH_ASSERT(r.has_value());
    return data;
}

// static
data_chunk utxo_entry::to_data_with_fixed(domain::chain::output const& output, data_chunk const& fixed) {
    auto const size = output.serialized_size(false) + fixed.size();
    data_chunk data(size);
    byte_writer writer(data);
    auto const r = to_data_with_fixed(writer, output, fixed);
    KTH_ASSERT(r.has_value());
    return data;
}


// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<utxo_entry> utxo_entry::from_data(byte_reader& reader) {
    auto output = domain::chain::output::from_data(reader, false);
    if ( ! output) {
        return std::unexpected(output.error());
    }

    auto const height = reader.read_little_endian<uint32_t>();
    if ( ! height) {
        return std::unexpected(height.error());
    }

    auto const median_time_past = reader.read_little_endian<uint32_t>();
    if ( ! median_time_past) {
        return std::unexpected(median_time_past.error());
    }

    auto const coinbase = reader.read_byte();
    if ( ! coinbase) {
        return std::unexpected(coinbase.error());
    }

    return utxo_entry(std::move(*output), *height, *median_time_past, *coinbase);
}


// Serialization.
//-----------------------------------------------------------------------------

data_chunk utxo_entry::to_data() const {
    return kth::to_data_chunk(*this);
}

} // namespace kth::database
