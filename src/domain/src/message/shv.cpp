// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/shv.hpp>

#include <utility>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

std::string const shv::command = "shv";
uint32_t const shv::version_minimum = version::level::minimum;
uint32_t const shv::version_maximum = version::level::maximum;

expect<shv_response> shv_response::from_data(byte_reader& reader) {
    auto const type = reader.read_byte();
    if ( ! type) return std::unexpected(type.error());
    auto const commitment = reader.read_variable_little_endian();
    if ( ! commitment) return std::unexpected(commitment.error());
    auto const height = reader.read_variable_little_endian();
    if ( ! height) return std::unexpected(height.error());
    auto header = chain::header::from_data(reader, true);
    if ( ! header) return std::unexpected(header.error());
    auto const count = reader.read_size_little_endian();
    if ( ! count) return std::unexpected(count.error());
    // A uint64_t leaf count needs at most 64 siblings, even with padding.
    if (*count > 64) return std::unexpected(error::invalid_size);
    hash_list proof;
    proof.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto hash = read_hash(reader);
        if ( ! hash) return std::unexpected(hash.error());
        proof.push_back(*hash);
    }
    auto const target = read_hash(reader);
    if ( ! target) return std::unexpected(target.error());
    return shv_response{*type, *commitment, *height, std::move(*header), std::move(proof), *target};
}

expect<void> shv_response::to_data(byte_writer& writer) const noexcept {
    if (auto r = writer.write_byte(type); ! r) return r;
    if (auto r = writer.write_variable_little_endian(commitment_height); ! r) return r;
    if (auto r = writer.write_variable_little_endian(block_height); ! r) return r;
    if (auto r = header.to_data(writer, true); ! r) return r;
    if (auto r = writer.write_variable_little_endian(proof.size()); ! r) return r;
    for (auto const& hash : proof) {
        if (auto r = writer.write_hash(hash); ! r) return r;
    }
    return writer.write_hash(target);
}

size_t shv_response::serialized_size() const {
    return 1 + infrastructure::message::variable_uint_size(commitment_height)
        + infrastructure::message::variable_uint_size(block_height) + header.serialized_size(true)
        + infrastructure::message::variable_uint_size(proof.size()) + proof.size() * hash_size + hash_size;
}

expect<shv> shv::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const count = reader.read_size_little_endian();
    if ( ! count) return std::unexpected(count.error());
    if (*count > max_shv_requests) return std::unexpected(error::invalid_size);

    shv result;
    result.responses.reserve(*count);
    for (size_t i = 0; i < *count; ++i) {
        auto response = shv_response::from_data(reader);
        if ( ! response) return std::unexpected(response.error());
        result.responses.push_back(std::move(*response));
    }
    if ( ! reader.is_exhausted()) return std::unexpected(error::invalid_size);
    return result;
}

expect<void> shv::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
    if (auto r = writer.write_variable_little_endian(responses.size()); ! r) return r;
    for (auto const& response : responses) {
        if (auto r = response.to_data(writer); ! r) return r;
    }
    return {};
}

size_t shv::serialized_size(uint32_t /*version*/) const {
    auto size = infrastructure::message::variable_uint_size(responses.size());
    for (auto const& response : responses) {
        size += response.serialized_size();
    }
    return size;
}

} // namespace kth::domain::message
