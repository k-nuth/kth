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

    prefilled_transaction();
    prefilled_transaction(uint64_t index, chain::transaction const& tx);
    prefilled_transaction(uint64_t index, chain::transaction&& tx);
    prefilled_transaction(prefilled_transaction const& x) = default;
    prefilled_transaction(prefilled_transaction&& x) = default;

    prefilled_transaction& operator=(prefilled_transaction&& x) = default;
    prefilled_transaction& operator=(prefilled_transaction const& x) = default;

    [[nodiscard]]
    friend bool operator==(prefilled_transaction const&, prefilled_transaction const&) = default;


    [[nodiscard]]
    uint64_t index() const;

    void set_index(uint64_t value);

    chain::transaction& transaction();

    [[nodiscard]]
    chain::transaction const& transaction() const;

    void set_transaction(chain::transaction const& tx);
    void set_transaction(chain::transaction&& tx);

    static
    expect<prefilled_transaction> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


private:
    uint64_t index_;
    chain::transaction transaction_;
};

} // namespace kth::domain::message

#endif
