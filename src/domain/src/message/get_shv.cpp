// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_shv.hpp>

#include <algorithm>
#include <utility>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

std::string const get_shv::command = "getshv";
uint32_t const get_shv::version_minimum = version::level::minimum;
uint32_t const get_shv::version_maximum = version::level::maximum;

bool shv_request::is_proof_to_root() const noexcept {
    return (type & shv_proof_to_root) != 0;
}

bool shv_request::is_by_hash() const noexcept {
    return (type & shv_by_hash) != 0;
}

bool shv_request::is_valid() const noexcept {
    return (type & shv_reserved_mask) == 0 && (is_by_hash()
        || std::all_of(block_height_or_hash.begin() + 8, block_height_or_hash.end(),
            [](uint8_t value) { return value == 0; }));
}

uint64_t shv_request::block_height() const noexcept {
    uint64_t height = 0;
    for (size_t i = 0; i < 8; ++i) {
        height |= uint64_t{block_height_or_hash[i]} << (i * 8);
    }
    return height;
}

expect<shv_request> shv_request::from_data(byte_reader& reader) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const commitment = reader.read_variable_little_endian();
    if ( ! commitment) return std::unexpected(commitment.error());
    auto const block = read_hash(reader);
    if ( ! block) return std::unexpected(block.error());
    // Semantic errors are omitted individually by the server, like BCHN.
    return shv_request{*type, *commitment, *block};
}

expect<void> shv_request::to_data(byte_writer& writer) const noexcept {
    if (auto r = writer.write_byte(type); ! r) return r;
    if (auto r = writer.write_variable_little_endian(commitment_height); ! r) return r;
    return writer.write_hash(block_height_or_hash);
}

size_t shv_request::serialized_size() const {
    return 1 + infrastructure::message::variable_uint_size(commitment_height) + hash_size;
}

expect<get_shv> get_shv::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const count = reader.read_size_little_endian();
    if ( ! count) return std::unexpected(count.error());
    if (*count > max_shv_requests) return std::unexpected(error::invalid_size);

    get_shv result;
    result.requests.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto request = shv_request::from_data(reader);
        if ( ! request) return std::unexpected(request.error());
        result.requests.push_back(std::move(*request));
    }
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return result;
}

expect<void> get_shv::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (auto r = writer.write_variable_little_endian(requests.size()); ! r) return r;
    for (auto const& request : requests) {
        if (auto r = request.to_data(writer); ! r) return r;
    }
    return {};
}

size_t get_shv::serialized_size(uint32_t /*version*/) const {
    auto size = infrastructure::message::variable_uint_size(requests.size());
    for (auto const& request : requests) {
        size += request.serialized_size();
    }
    return size;
}

} // namespace kth::domain::message
