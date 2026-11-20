// Copyright (c) 2016-present Knuth Project developers.
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

get_block_transactions::get_block_transactions(hash_digest const& block_hash, std::vector<uint64_t> indexes)
    : block_hash_(block_hash)
    , indexes_(std::move(indexes))
{}

// static
expect<get_block_transactions> get_block_transactions::create(hash_digest const& block_hash, std::vector<uint64_t> indexes) {
    uint64_t previous = 0;
    bool started = false;
    for (auto const index : indexes) {
        if (index >= max_compact_transactions() || (started && index <= previous)) {
            return std::unexpected(error::invalid_size);
        }
        previous = index;
        started = true;
    }
    return get_block_transactions(block_hash, std::move(indexes));
}

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
    // Each index needs at least one byte. Check before reserving memory.
    if (*count > max_compact_transactions() || *count > reader.remaining_size()) {
        return std::unexpected(error::invalid_size);
    }
    std::vector<uint64_t> indexes;
    indexes.reserve(*count);
    uint64_t next = 0;
    for (size_t i = 0; i < *count; ++i) {
        auto const differential = reader.read_variable_little_endian();
        if ( ! differential) {
            return std::unexpected(differential.error());
        }
        if (*differential >= max_compact_transactions() - next) {
            return std::unexpected(error::invalid_size);
        }
        auto const absolute = next + *differential;
        indexes.push_back(absolute);
        next = absolute + 1;
    }
    return create(*block_hash, std::move(indexes));
}

// Serialization.
//-----------------------------------------------------------------------------

size_t get_block_transactions::serialized_size(uint32_t /*version*/) const {
    auto size = hash_size + infrastructure::message::variable_uint_size(indexes_.size());

    uint64_t next = 0;
    for (auto const element : indexes_) {
        auto const differential = element - next;
        next = element + 1;
        size += infrastructure::message::variable_uint_size(differential);
    }

    return size;
}

hash_digest const& get_block_transactions::block_hash() const {
    return block_hash_;
}

std::vector<uint64_t> const& get_block_transactions::indexes() const {
    return indexes_;
}

expect<void> get_block_transactions::to_data(byte_writer& writer, uint32_t version) const noexcept {
        if (auto r = writer.write_hash(block_hash_); ! r) return r;
        if (auto r = writer.write_variable_little_endian(indexes_.size()); ! r) return r;
        uint64_t next = 0;
        for (auto const element : indexes_) {
            auto const differential = element - next;
            if (auto r = writer.write_variable_little_endian(differential); ! r) return r;
            next = element + 1;
        }
        return {};
}

} // namespace kth::domain::message
