// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_HEADER_HPP
#define KTH_DOMAIN_MESSAGE_HEADER_HPP

#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/message_tools.hpp>

namespace kth::domain::message {

/// Message header - wraps chain::header with P2P wire serialization.
/// The P2P "headers" message includes a trailing zero byte per header.
struct KD_API header : chain::header {
    using list = std::vector<header>;
    using ptr = std::shared_ptr<header>;
    using const_ptr = std::shared_ptr<header const>;
    using ptr_list = std::vector<ptr>;
    using const_ptr_list = std::vector<const_ptr>;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

    // Inherit constructors
    using chain::header::header;

    header() = default;

    header(chain::header const& x)
        : chain::header(x)
    {}

    header(header const&) = default;
    header(header&&) = default;
    header& operator=(header const&) = default;
    header& operator=(header&&) = default;

    // P2P wire protocol size (includes trailing zero byte).
    static
    size_t satoshi_fixed_size(uint32_t version);

    // Deserialization from P2P wire (reads trailing zero byte).
    static
    expect<header> from_data(byte_reader& reader, uint32_t version);

    // Serialization to P2P wire (writes trailing zero byte).
    data_chunk to_data(uint32_t version) const;
    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t version, W& sink) const {
        chain::header::to_data(sink, true);  // wire=true

        if (version != version::level::canonical) {
            sink.write_variable_little_endian(uint64_t{0});
        }
    }

    size_t serialized_size(uint32_t version) const;
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_HEADER_HPP
