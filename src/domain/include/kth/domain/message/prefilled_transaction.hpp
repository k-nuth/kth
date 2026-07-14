// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_PREFILLED_TRANSACTION_HPP
#define KTH_DOMAIN_MESSAGE_PREFILLED_TRANSACTION_HPP

#include <istream>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API prefilled_transaction {
    using list = std::vector<prefilled_transaction>;
    using const_ptr = std::shared_ptr<const prefilled_transaction>;

    /// Build from parts. Unlike most domain types, construction here *can*
    /// yield a syntactically invalid object: BIP152 caps the prefilled index,
    /// and nothing else constrains the caller. Returns
    /// `error::invalid_compact_block` when `index` is out of range, so a
    /// constructed `prefilled_transaction` always carries a usable index.
    static
    expect<prefilled_transaction> create(uint64_t index, chain::transaction tx);

    prefilled_transaction(prefilled_transaction const& x) = default;
    prefilled_transaction(prefilled_transaction&& x) = default;

    prefilled_transaction& operator=(prefilled_transaction&& x) = default;
    prefilled_transaction& operator=(prefilled_transaction const& x) = default;

    [[nodiscard]]
    friend bool operator==(prefilled_transaction const&, prefilled_transaction const&) = default;


    [[nodiscard]]
    uint64_t index() const;

    [[nodiscard]]
    chain::transaction const& transaction() const;

    static
    expect<prefilled_transaction> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


private:
    // Construction goes through `create` / `from_data`, which guarantee an
    // in-range index.
    prefilled_transaction(uint64_t index, chain::transaction tx);

    uint64_t index_;
    chain::transaction transaction_;
};

} // namespace kth::domain::message

#endif
