// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_blocks.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/limits.hpp>
namespace kth::domain::message {

std::string const get_blocks::command = "getblocks";
uint32_t const get_blocks::version_minimum = version::level::minimum;
uint32_t const get_blocks::version_maximum = version::level::maximum;

get_blocks::get_blocks()
    : stop_hash_(null_hash) {
}

get_blocks::get_blocks(hash_list const& start, hash_digest const& stop)
    : start_hashes_(start), stop_hash_(stop) {
}

get_blocks::get_blocks(hash_list&& start, hash_digest const& stop)
    : start_hashes_(std::move(start)), stop_hash_(stop) {
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<get_blocks> get_blocks::from_data(byte_reader& reader, [[maybe_unused]] uint32_t version) {
    // Discard protocol version because it is stupid.
    auto const skipped_version = reader.skip(4);
    if ( ! skipped_version) {
        return std::unexpected(skipped_version.error());
    }

    auto const count = reader.read_size_little_endian();
    if ( ! count) {
        return std::unexpected(count.error());
    }

    hash_list start_hashes;
    start_hashes.reserve(*count);

    for (size_t i = 0; i < *count; ++i) {
        auto const start_hash = read_hash(reader);
        if ( ! start_hash) {
            return std::unexpected(start_hash.error());
        }
        start_hashes.emplace_back(*start_hash);
    }

    auto const stop_hash = read_hash(reader);
    if ( ! stop_hash) {
        return std::unexpected(stop_hash.error());
    }

    return get_blocks(std::move(start_hashes), *stop_hash);
}

// Serialization.
//-----------------------------------------------------------------------------



size_t get_blocks::serialized_size(uint32_t /*version*/) const {
    return size_t(36) + infrastructure::message::variable_uint_size(start_hashes_.size()) + hash_size * start_hashes_.size();
}

hash_list& get_blocks::start_hashes() {
    return start_hashes_;
}

hash_list const& get_blocks::start_hashes() const {
    return start_hashes_;
}

void get_blocks::set_start_hashes(hash_list const& value) {
    start_hashes_ = value;
}

void get_blocks::set_start_hashes(hash_list&& value) {
    start_hashes_ = std::move(value);
}

hash_digest& get_blocks::stop_hash() {
    return stop_hash_;
}

hash_digest const& get_blocks::stop_hash() const {
    return stop_hash_;
}

void get_blocks::set_stop_hash(hash_digest const& value) {
    stop_hash_ = value;
}

expect<void> get_blocks::to_data(byte_writer& writer, uint32_t version) const {
        if (auto r = writer.write_little_endian<uint32_t>(version); ! r) return r;
        if (auto r = writer.write_variable_little_endian(start_hashes_.size()); ! r) return r;

        for (auto const& start_hash : start_hashes_) {
            if (auto r = writer.write_hash(start_hash); ! r) return r;
        }

        if (auto r = writer.write_hash(stop_hash_); ! r) return r;
        return {};
}

} // namespace kth::domain::message
