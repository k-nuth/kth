// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_FILTER_ADD_HPP
#define KTH_DOMAIN_MESSAGE_FILTER_ADD_HPP

#include <memory>
#include <string>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API filter_add {
    using ptr = std::shared_ptr<filter_add>;
    using const_ptr = std::shared_ptr<const filter_add>;

    filter_add() = default;

    /// Fails with error::invalid_filter_add over max_filter_add bytes (BIP37).
    static
    expect<filter_add> create(data_chunk data);

    [[nodiscard]]
    friend bool operator==(filter_add const&, filter_add const&) = default;

    [[nodiscard]]
    data_chunk const& data() const;

    static
    expect<filter_add> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

private:
    filter_add(data_chunk data);

    data_chunk data_;
};

} // namespace kth::domain::message

#endif
