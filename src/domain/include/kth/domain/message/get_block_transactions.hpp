// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_BLOCK_TRANSACTIONS_HPP
#define KTH_DOMAIN_MESSAGE_GET_BLOCK_TRANSACTIONS_HPP

#include <memory>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API get_block_transactions {
    using ptr = std::shared_ptr<get_block_transactions>;
    using const_ptr = std::shared_ptr<const get_block_transactions>;

    get_block_transactions();

    /// Fails with error::invalid_size over the number of transactions a block
    /// could hold.
    static
    expect<get_block_transactions> create(hash_digest const& block_hash, std::vector<uint64_t> indexes);

    [[nodiscard]]
    friend bool operator==(get_block_transactions const&, get_block_transactions const&) = default;

    [[nodiscard]]
    hash_digest const& block_hash() const;

    [[nodiscard]]
    std::vector<uint64_t> const& indexes() const;


    static
    expect<get_block_transactions> from_data(byte_reader& reader, uint32_t /*version*/);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

private:
    get_block_transactions(hash_digest const& block_hash, std::vector<uint64_t> indexes);

    hash_digest block_hash_;
    std::vector<uint64_t> indexes_;
};

} // namespace kth::domain::message

#endif
