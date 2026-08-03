// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/block_undo.hpp>

#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::database {

// =============================================================================
// spent_output
// =============================================================================

size_t spent_output::serialized_size() const {
    return std::tuple_size<utxoz::raw_outpoint>::value                      // key
         + infrastructure::message::variable_uint_size(value.size())        // value length
         + value.size()                                                     // value
         + sizeof(uint32_t);                                                // height
}

expect<void> spent_output::to_data(byte_writer& writer) const {
    if (auto r = writer.write_bytes(key); ! r) return r;
    if (auto r = writer.write_variable_little_endian(value.size()); ! r) return r;
    if (auto r = writer.write_bytes(value); ! r) return r;
    return writer.write_little_endian<uint32_t>(height);
}

std::expected<spent_output, result_code> spent_output::from_data(byte_reader& reader) {
    spent_output result;

    auto key = reader.read_array<std::tuple_size<utxoz::raw_outpoint>::value>();
    if ( ! key) {
        return std::unexpected(result_code::other);
    }
    result.key = *key;

    auto const size = reader.read_variable_little_endian();
    if ( ! size) {
        return std::unexpected(result_code::other);
    }

    auto value = reader.read_bytes(*size);
    if ( ! value) {
        return std::unexpected(result_code::other);
    }
    result.value.assign(value->begin(), value->end());

    auto const height = reader.read_little_endian<uint32_t>();
    if ( ! height) {
        return std::unexpected(result_code::other);
    }
    result.height = *height;

    return result;
}

// =============================================================================
// block_undo
// =============================================================================

size_t block_undo::serialized_size() const {
    size_t size = infrastructure::message::variable_uint_size(spent.size());
    for (auto const& entry : spent) {
        size += entry.serialized_size();
    }
    return size;
}

expect<void> block_undo::to_data(byte_writer& writer) const {
    if (auto r = writer.write_variable_little_endian(spent.size()); ! r) return r;
    for (auto const& entry : spent) {
        if (auto r = entry.to_data(writer); ! r) return r;
    }
    return {};
}

data_chunk block_undo::to_data() const {
    return kth::to_data_chunk(*this);
}

std::expected<block_undo, result_code> block_undo::from_data(byte_reader& reader) {
    block_undo result;

    auto const count = reader.read_variable_little_endian();
    if ( ! count) {
        return std::unexpected(result_code::other);
    }

    // No reserve(): `count` comes straight off disk, so trusting it would let a
    // corrupt record throw length_error/bad_alloc instead of returning a decode
    // failure. The loop below fails cleanly as soon as the reader runs dry.
    for (uint64_t i = 0; i < *count; ++i) {
        auto entry = spent_output::from_data(reader);
        if ( ! entry) {
            return std::unexpected(entry.error());
        }
        result.spent.push_back(std::move(*entry));
    }

    return result;
}

} // namespace kth::database
