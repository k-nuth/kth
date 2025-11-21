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

    data_chunk to_data() const;
    void to_data(std::ostream& stream) const;

    template <typename W, KTH_IS_WRITER(W)>
    void to_data(W& sink) const {
        output_.to_data(sink, false);
        to_data_fixed(sink, height_, median_time_past_, coinbase_);
    }

    static
    expect<utxo_entry> from_data(byte_reader& reader);

    static
    data_chunk to_data_fixed(uint32_t height, uint32_t median_time_past, bool coinbase);

    static
    void to_data_fixed(std::ostream& stream, uint32_t height, uint32_t median_time_past, bool coinbase);

    template <typename W, KTH_IS_WRITER(W)>
    static
    void to_data_fixed(W& sink, uint32_t height, uint32_t median_time_past, bool coinbase) {
        sink.write_4_bytes_little_endian(height);
        sink.write_4_bytes_little_endian(median_time_past);
        sink.write_byte(coinbase);
    }

    static
    data_chunk to_data_with_fixed(domain::chain::output const& output, data_chunk const& fixed);

    static
    void to_data_with_fixed(std::ostream& stream, domain::chain::output const& output, data_chunk const& fixed);

    template <typename W, KTH_IS_WRITER(W)>
    static
    void to_data_with_fixed(W& sink, domain::chain::output const& output, data_chunk const& fixed) {
        output.to_data(sink, false);
        sink.write_bytes(fixed);
    }

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
