// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_BLOCK_TRANSACTIONS_HPP
#define KTH_DOMAIN_MESSAGE_BLOCK_TRANSACTIONS_HPP

#include <istream>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API block_transactions {
    using ptr = std::shared_ptr<block_transactions>;
    using const_ptr = std::shared_ptr<const block_transactions>;

    block_transactions();
    block_transactions(hash_digest const& block_hash, chain::transaction::list const& transactions);
    block_transactions(hash_digest const& block_hash, chain::transaction::list&& transactions);

    // block_transactions(block_transactions const& x) = default;
    // block_transactions(block_transactions&& x) = default;
    // // This class is move assignable but not copy assignable.
    // block_transactions& operator=(block_transactions&& x) = default;
    // block_transactions& operator=(block_transactions const&) = default;

    bool operator==(block_transactions const& x) const;
    bool operator!=(block_transactions const& x) const;


    hash_digest& block_hash();

    [[nodiscard]]
    hash_digest const& block_hash() const;

    void set_block_hash(hash_digest const& value);

    chain::transaction::list& transactions();

    [[nodiscard]]
    chain::transaction::list const& transactions() const;

    void set_transactions(chain::transaction::list const& x);
    void set_transactions(chain::transaction::list&& x);


    static
    expect<block_transactions> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W& sink) const {
        sink.write_hash(block_hash_);
        sink.write_variable_little_endian(transactions_.size());

        for (auto const& element : transactions_) {
            element.to_data(sink, /*wire*/ true);
        }
    }

    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

private:
    hash_digest block_hash_;
    chain::transaction::list transactions_;
};

} // namespace kth::domain::message

#endif
