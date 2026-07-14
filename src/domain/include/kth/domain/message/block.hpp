// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_BLOCK_HPP
#define KTH_DOMAIN_MESSAGE_BLOCK_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>


namespace kth::domain::message {

struct KD_API block : chain::block {
public:
    using ptr = std::shared_ptr<block>;
    using const_ptr = std::shared_ptr<const block>;
    using ptr_list = std::vector<ptr>;
    using const_ptr_list = std::vector<const_ptr>;
    using const_ptr_list_ptr = std::shared_ptr<const_ptr_list>;
    using const_ptr_list_const_ptr = std::shared_ptr<const const_ptr_list>;

    // Wrapping an already-constructed (hence valid) chain::block.
    block(chain::block const& x);
    block(chain::block&& x);

    /// Build from parts. Mirrors `chain::block::create`; construction cannot
    /// fail (a domain block does no consensus checks).
    static
    block create(chain::header header, chain::transaction::list transactions);

    block& operator=(chain::block&& x);

    bool operator==(chain::block const& x) const;
    bool operator!=(chain::block const& x) const;
    bool operator==(block const& x) const;
    bool operator!=(block const& x) const;

    // Serialization.
    //-------------------------------------------------------------------------
    static
    expect<block> from_data(byte_reader& reader, uint32_t /*version*/);

    // Serialization.
    //-------------------------------------------------------------------------


    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    size_t serialized_size(uint32_t version) const;


    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;
};

hash_digest hash(block const& block, uint64_t nonce);

} // namespace kth::domain::message

#endif
