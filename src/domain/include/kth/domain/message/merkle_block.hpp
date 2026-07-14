// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_MERKLE_BLOCK_HPP
#define KTH_DOMAIN_MESSAGE_MERKLE_BLOCK_HPP

#include <istream>
#include <memory>
#include <string>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API merkle_block {
    using list = std::vector<merkle_block>;
    using ptr = std::shared_ptr<merkle_block>;
    using const_ptr = std::shared_ptr<const merkle_block>;

    /// Build from parts. Construction cannot fail (no consensus checks).
    static
    merkle_block create(chain::header header, size_t total_transactions, hash_list hashes, data_chunk flags);

    /// Wrap an already-constructed (hence valid) block.
    merkle_block(chain::block const& block);

    [[nodiscard]]
    friend bool operator==(merkle_block const&, merkle_block const&) = default;

    [[nodiscard]]
    chain::header const& header() const;

    [[nodiscard]]
    size_t total_transactions() const;

    [[nodiscard]]
    hash_list const& hashes() const;

    [[nodiscard]]
    data_chunk const& flags() const;

    static
    expect<merkle_block> from_data(byte_reader& reader, uint32_t version);

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
    // Construction goes through `create` / `from_data` / the block-wrapping
    // ctor, which guarantee a non-sentinel value.
    merkle_block(chain::header const& header, size_t total_transactions, hash_list&& hashes, data_chunk&& flags);
    merkle_block(chain::header const& header, size_t total_transactions, hash_list const& hashes, data_chunk const& flags);

    chain::header header_;
    size_t total_transactions_{0};
    hash_list hashes_;
    data_chunk flags_;
};

} // namespace kth::domain::message

#endif
