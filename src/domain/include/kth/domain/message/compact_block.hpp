// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_COMPACT_BLOCK_HPP
#define KTH_DOMAIN_MESSAGE_COMPACT_BLOCK_HPP

#include <istream>

#include <kth/domain/chain/header.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/message/block.hpp>
#include <kth/domain/message/prefilled_transaction.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API compact_block {
    using ptr = std::shared_ptr<compact_block>;
    using const_ptr = std::shared_ptr<const compact_block>;
    //using short_id = mini_hash;
    //using short_id_list = mini_hash_list;
    using short_id = uint64_t;
    using short_id_list = std::vector<short_id>;

    /// Derive a compact block from an (already valid) block.
    static
    compact_block factory_from_block(message::block const& blk);

    // Construction cannot fail (no consensus checks); every state, including
    // the all-default one, is syntactically valid.
    compact_block() = default;
    compact_block(chain::header header, uint64_t nonce, short_id_list short_ids, prefilled_transaction::list transactions);

    [[nodiscard]]
    friend bool operator==(compact_block const&, compact_block const&) = default;

    [[nodiscard]]
    chain::header const& header() const;

    [[nodiscard]]
    uint64_t nonce() const;

    [[nodiscard]]
    short_id_list const& short_ids() const;

    [[nodiscard]]
    prefilled_transaction::list const& transactions() const;

    static
    expect<compact_block> from_data(byte_reader& reader, uint32_t version);

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
    chain::header header_;
    uint64_t nonce_{0};
    short_id_list short_ids_;
    prefilled_transaction::list transactions_;
};

hash_digest hash(compact_block const& block);

} // namespace kth::domain::message

#endif
