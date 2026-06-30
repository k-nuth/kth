// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_UTXO_ENTRY_HPP_
#define KTH_DATABASE_UTXO_ENTRY_HPP_

#include <kth/domain.hpp>
#include <kth/database/define.hpp>

namespace kth::database {

struct KD_API utxo_entry {

    utxo_entry() = default;

    utxo_entry(domain::chain::output output, uint32_t height, uint32_t median_time_past, bool coinbase);

    // Getters
    domain::chain::output const& output() const;
    uint32_t height() const;
    uint32_t median_time_past() const;
    bool coinbase() const;

    bool is_valid() const;

    size_t serialized_size() const;

    static
    expect<utxo_entry> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const {
        if (auto r = output_.to_data(writer, false); ! r) return r;
        return to_data_fixed(writer, height_, median_time_past_, coinbase_);
    }

    data_chunk to_data() const;

    [[nodiscard]]
    static
    expect<void> to_data_fixed(byte_writer& writer, uint32_t height, uint32_t median_time_past, bool coinbase) {
        if (auto r = writer.write_little_endian<uint32_t>(height); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(median_time_past); ! r) return r;
        return writer.write_byte(coinbase ? 1 : 0);
    }

    static
    data_chunk to_data_fixed(uint32_t height, uint32_t median_time_past, bool coinbase);

    [[nodiscard]]
    static
    expect<void> to_data_with_fixed(byte_writer& writer, domain::chain::output const& output, data_chunk const& fixed) {
        if (auto r = output.to_data(writer, false); ! r) return r;
        return writer.write_bytes(fixed);
    }

    static
    data_chunk to_data_with_fixed(domain::chain::output const& output, data_chunk const& fixed);

private:
    void reset();

    constexpr static
    size_t serialized_size_fixed();

    domain::chain::output output_;
    uint32_t height_ = max_uint32;
    uint32_t median_time_past_ = max_uint32;
    bool coinbase_;
};

} // namespace kth::database

#endif // KTH_DATABASE_UTXO_ENTRY_HPP_
