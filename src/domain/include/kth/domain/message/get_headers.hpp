// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_GET_HEADERS_HPP
#define KTH_DOMAIN_MESSAGE_GET_HEADERS_HPP

#include <istream>
#include <memory>
#include <string>
#include <string_view>

#include <kth/domain/message/version.hpp>
#include <kth/domain/message/get_blocks.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API get_headers : get_blocks {
public:
    using ptr = std::shared_ptr<get_headers>;
    using const_ptr = std::shared_ptr<const get_headers>;

    get_headers() = default;
    get_headers(hash_list const& start, hash_digest const& stop);
    get_headers(hash_list&& start, hash_digest const& stop);

    [[nodiscard]]
    friend bool operator==(get_headers const&, get_headers const&) = default;

    static
    expect<get_headers> from_data(byte_reader& reader, uint32_t version);

    static constexpr std::string_view command = "getheaders";
    static constexpr uint32_t version_minimum = version::level::headers;
    static constexpr uint32_t version_maximum = version::level::maximum;
};

} // namespace kth::domain::message

#endif
