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
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API merkle_block {
    using list = std::vector<merkle_block>;
    using ptr = std::shared_ptr<merkle_block>;
    using const_ptr = std::shared_ptr<const merkle_block>;

    merkle_block() = default;
    merkle_block(chain::header const& header, size_t total_transactions, hash_list const& hashes, data_chunk const& flags);
    merkle_block(chain::header const& header, size_t total_transactions, hash_list&& hashes, data_chunk&& flags);
    merkle_block(chain::block const& block);

    bool operator==(merkle_block const& x) const;
    bool operator!=(merkle_block const& x) const;

    chain::header& header();

    [[nodiscard]]
    chain::header const& header() const;

    void set_header(chain::header const& value);

    [[nodiscard]]
    size_t total_transactions() const;

    void set_total_transactions(size_t value);

    hash_list& hashes();

    [[nodiscard]]
    hash_list const& hashes() const;

    void set_hashes(hash_list const& value);
    void set_hashes(hash_list&& value);

    data_chunk& flags();

    [[nodiscard]]
    data_chunk const& flags() const;

    void set_flags(data_chunk const& value);
    void set_flags(data_chunk&& value);

    static
    expect<merkle_block> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W& sink) const {
        header_.to_data(sink);

        auto const total32 = *safe_unsigned<uint32_t>(total_transactions_);
        sink.write_4_bytes_little_endian(total32);
        sink.write_variable_little_endian(hashes_.size());

        for (auto const& hash : hashes_) {
            sink.write_hash(hash);
        }

        sink.write_variable_little_endian(flags_.size());
        sink.write_bytes(flags_);
    }

    //void to_data(uint32_t version, writer& sink) const;
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
    chain::header header_;
    size_t total_transactions_{0};
    hash_list hashes_;
    data_chunk flags_;
};

} // namespace kth::domain::message

#endif
