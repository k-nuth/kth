// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_SHV_HPP
#define KTH_DOMAIN_MESSAGE_SHV_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/get_shv.hpp>

namespace kth::domain::message {

struct KD_API shv_response {
    uint8_t type{0};
    uint64_t commitment_height{0};
    uint64_t block_height{0};
    chain::header header{0u, null_hash, null_hash, 0u, 0u, 0u};
    hash_list proof;
    hash_digest target{null_hash};

    [[nodiscard]]
    friend bool operator==(shv_response const&, shv_response const&) = default;

    static expect<shv_response> from_data(byte_reader& reader);
    [[nodiscard]] expect<void> to_data(byte_writer& writer) const noexcept;
    [[nodiscard]] size_t serialized_size() const;
};

struct KD_API shv {
    using ptr = std::shared_ptr<shv>;
    using const_ptr = std::shared_ptr<const shv>;

    std::vector<shv_response> responses;

    [[nodiscard]]
    friend bool operator==(shv const&, shv const&) = default;

    static expect<shv> from_data(byte_reader& reader, uint32_t version);
    [[nodiscard]] expect<void> to_data(byte_writer& writer, uint32_t version) const noexcept;
    [[nodiscard]] size_t serialized_size(uint32_t version) const;

    static std::string const command;
    static uint32_t const version_minimum;
    static uint32_t const version_maximum;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_SHV_HPP
