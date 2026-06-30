// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_TRANSACTION_UNCONFIRMED_ENTRY_HPP_
#define KTH_DATABASE_TRANSACTION_UNCONFIRMED_ENTRY_HPP_

#include <kth/domain.hpp>

#include <kth/database/currency_config.hpp>
#include <kth/database/define.hpp>

namespace kth::database {


struct KD_API transaction_unconfirmed_entry {

    transaction_unconfirmed_entry() = default;

    transaction_unconfirmed_entry(domain::chain::transaction const& tx, uint32_t arrival_time, uint32_t height);

    // Getters
    domain::chain::transaction const& transaction() const;
    uint32_t arrival_time() const;
    uint32_t height() const;

    bool is_valid() const;

    //TODO(fernando): make this constexpr
    // constexpr
    static
    size_t serialized_size(domain::chain::transaction const& tx);

    static
    expect<transaction_unconfirmed_entry> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const {
        return factory_to_data(writer, transaction_, arrival_time_, height_);
    }

    data_chunk to_data() const;

    [[nodiscard]]
    static
    expect<void> factory_to_data(byte_writer& writer, domain::chain::transaction const& tx, uint32_t arrival_time, uint32_t height) {
        if (auto r = tx.to_data(writer, false); ! r) return r;
        if (auto r = writer.write_little_endian<uint32_t>(arrival_time); ! r) return r;
        return writer.write_little_endian<uint32_t>(height);
    }

    static
    data_chunk factory_to_data(domain::chain::transaction const& tx, uint32_t arrival_time, uint32_t height);

private:
    void reset();

    domain::chain::transaction transaction_;
    uint32_t arrival_time_;
    uint32_t height_;
};

} // namespace kth::database


#endif // KTH_DATABASE_TRANSACTION_UNCONFIRMED_ENTRY_HPP_
