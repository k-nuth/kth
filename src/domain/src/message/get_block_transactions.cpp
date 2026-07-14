// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_block_transactions.hpp>

#include <initializer_list>

#include <kth/domain/message/version.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/limits.hpp>
namespace kth::domain::message {

std::string const get_block_transactions::command = "getblocktxn";
uint32_t const get_block_transactions::version_minimum = version::level::bip152;
uint32_t const get_block_transactions::version_maximum = version::level::bip152;

get_block_transactions::get_block_transactions()
    : block_hash_(null_hash)
{}

get_block_transactions::get_block_transactions(hash_digest const& block_hash, std::vector<uint64_t> const& indexes)
    : block_hash_(block_hash)
    , indexes_(indexes)
{}

get_block_transactions::get_block_transactions(hash_digest const& block_hash, std::vector<uint64_t>&& indexes)
    : block_hash_(block_hash)
    , indexes_(std::move(indexes))
{}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<get_block_transactions> get_block_transactions::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const block_hash = read_hash(reader);
    if ( ! block_hash) {
        return std::unexpected(block_hash.error());
    }
    auto const count = reader.read_size_little_endian();
    if ( ! count) {
        return std::unexpected(count.error());
    }
    if (*count > static_absolute_max_block_size()) {
        return std::unexpected(error::invalid_size);
    }
    std::vector<uint64_t> indexes;
    indexes.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto const index = reader.read_size_little_endian();
        if ( ! index) {
            return std::unexpected(index.error());
        }
        indexes.push_back(*index);
    }
    return get_block_transactions(*block_hash, std::move(indexes));
}

// Serialization.
//-----------------------------------------------------------------------------



size_t get_block_transactions::serialized_size(uint32_t /*version*/) const {
    auto size = hash_size + infrastructure::message::variable_uint_size(indexes_.size());

    for (auto const& element : indexes_) {
        size += infrastructure::message::variable_uint_size(element);
    }

    return size;
}

hash_digest& get_block_transactions::block_hash() {
    return block_hash_;
}

hash_digest const& get_block_transactions::block_hash() const {
    return block_hash_;
}

void get_block_transactions::set_block_hash(hash_digest const& value) {
    block_hash_ = value;
}

std::vector<uint64_t>& get_block_transactions::indexes() {
    return indexes_;
}

std::vector<uint64_t> const& get_block_transactions::indexes() const {
    return indexes_;
}

void get_block_transactions::set_indexes(std::vector<uint64_t> const& values) {
    indexes_ = values;
}

void get_block_transactions::set_indexes(std::vector<uint64_t>&& values) {
    indexes_ = values;
}

expect<void> get_block_transactions::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_hash(block_hash_); ! r) return r;
        if (auto r = writer.write_variable_little_endian(indexes_.size()); ! r) return r;
        for (auto const& element : indexes_) {
            if (auto r = writer.write_variable_little_endian(element); ! r) return r;
        }
        return {};
}

} // namespace kth::domain::message
