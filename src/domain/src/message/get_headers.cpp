// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/get_headers.hpp>

#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>

namespace kth::domain::message {

std::string const get_headers::command = "getheaders";
uint32_t const get_headers::version_minimum = version::level::headers;
uint32_t const get_headers::version_maximum = version::level::maximum;

get_headers::get_headers(hash_list const& start, hash_digest const& stop)
    : get_blocks(start, stop) {
}

//TODO(fernando): move semantics lost
get_headers::get_headers(hash_list&& start, hash_digest const& stop)
    : get_headers(start, stop)
{}

bool get_headers::operator==(get_headers const& x) const {
    return (static_cast<get_blocks const&>(*this) == static_cast<get_blocks const&>(x));
}

bool get_headers::operator!=(get_headers const& x) const {
    return !(*this == x);
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<get_headers> get_headers::from_data(byte_reader& reader, uint32_t version) {
    auto blocks = get_blocks::from_data(reader, version);
    if ( ! blocks) {
        return std::unexpected(blocks.error());
    }
    if (version < get_headers::version_minimum) {
        return std::unexpected(error::version_too_new);
    }
    return get_headers(std::move(blocks->start_hashes()), blocks->stop_hash());
}


} // namespace kth::domain::message
