// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_TRANSACTION_ENTRY_HPP_
#define KTH_DATABASE_TRANSACTION_ENTRY_HPP_

#include <kth/domain.hpp>

#include <kth/database/currency_config.hpp>
#include <kth/database/define.hpp>

namespace kth::database {


inline
expect<void> write_position(byte_writer& writer, uint32_t position) {
    return writer.write_little_endian<kth_position_t>(static_cast<kth_position_t>(position));
}

inline
expect<kth_position_t> read_position(byte_reader& reader) {
    return reader.read_little_endian<kth_position_t>();
}

struct KD_API transaction_entry {

    transaction_entry() = default;

    transaction_entry(domain::chain::transaction const& tx, uint32_t height, uint32_t median_time_past, uint32_t position);

    // Getters
    domain::chain::transaction const& transaction() const;
    uint32_t height() const;
    uint32_t median_time_past() const;
    uint32_t position() const;

    bool is_valid() const;

    //TODO(fernando): make this constexpr
    // constexpr
    static
    size_t serialized_size(domain::chain::transaction const& tx);

    // Instance-side wrapper so `transaction_entry` satisfies
    // `kth::Serializable` and can flow through `kth::to_data_chunk`,
    // matching the free-function member/static split we already have
    // for `to_data`.
    [[nodiscard]]
    size_t serialized_size() const {
        return serialized_size(transaction_);
    }

    static
    expect<transaction_entry> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const {
        return factory_to_data(writer, transaction_, height_, median_time_past_, position_);
    }

    data_chunk to_data() const;

    bool confirmed() const;

    //TODO(kth): we don't have spent information
    //bool is_spent(size_t fork_height) const;

    [[nodiscard]]
    static
    expect<void> factory_to_data(byte_writer& writer, domain::chain::transaction const& tx, uint32_t height, uint32_t median_time_past, uint32_t position) {
        if (auto r = tx.to_data(writer, false); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(height); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(median_time_past); ! r) return r;
        return write_position(writer, position);
    }

    static
    data_chunk factory_to_data(domain::chain::transaction const& tx, uint32_t height, uint32_t median_time_past, uint32_t position);

private:
    void reset();

    domain::chain::transaction transaction_;
    uint32_t height_;
    uint32_t median_time_past_;
    uint32_t position_;
};

} // namespace kth::database


#endif // KTH_DATABASE_TRANSACTION_ENTRY_HPP_
