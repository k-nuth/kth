// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_SHV_HPP
#define KTH_DOMAIN_MESSAGE_GET_SHV_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth::domain::message {

inline constexpr size_t max_shv_requests = 100;
inline constexpr uint8_t shv_proof_to_root = 0x01;
inline constexpr uint8_t shv_by_hash = 0x02;
inline constexpr uint8_t shv_reserved_mask = 0xfc;

struct KD_API shv_request {
    uint8_t type{0};
    uint64_t commitment_height{0};
    hash_digest block_height_or_hash{null_hash};

    [[nodiscard]] bool is_proof_to_root() const noexcept;
    [[nodiscard]] bool is_by_hash() const noexcept;
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] uint64_t block_height() const noexcept;

    [[nodiscard]]
    friend bool operator==(shv_request const&, shv_request const&) = default;

    static expect<shv_request> from_data(byte_reader& reader);
    [[nodiscard]] expect<void> to_data(byte_writer& writer) const noexcept;
    [[nodiscard]] size_t serialized_size() const;
};

struct KD_API get_shv {
    using ptr = std::shared_ptr<get_shv>;
    using const_ptr = std::shared_ptr<const get_shv>;

    std::vector<shv_request> requests;

    [[nodiscard]]
    friend bool operator==(get_shv const&, get_shv const&) = default;

    static expect<get_shv> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_GET_SHV_HPP
