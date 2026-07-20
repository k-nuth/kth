// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_FILTER_LOAD_HPP
#define KTH_DOMAIN_MESSAGE_FILTER_LOAD_HPP

#include <memory>
#include <string>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API filter_load {
    using ptr = std::shared_ptr<filter_load>;
    using const_ptr = std::shared_ptr<const filter_load>;

    filter_load() = default;

    /// Fails with error::invalid_filter_load over max_filter_load bytes or
    /// max_filter_functions hash functions (BIP37).
    static
    expect<filter_load> create(data_chunk filter, uint32_t hash_functions, uint32_t tweak, uint8_t flags);

    [[nodiscard]]
    friend bool operator==(filter_load const&, filter_load const&) = default;

    [[nodiscard]]
    data_chunk const& filter() const;


    [[nodiscard]]
    uint32_t hash_functions() const;

    [[nodiscard]]
    uint32_t tweak() const;

    [[nodiscard]]
    uint8_t flags() const;

    static
    expect<filter_load> from_data(byte_reader& reader, uint32_t version);

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
    filter_load(data_chunk filter, uint32_t hash_functions, uint32_t tweak, uint8_t flags);

    data_chunk filter_;
    uint32_t hash_functions_{0};
    uint32_t tweak_{0};
    uint8_t flags_{0x00};
};

} // namespace kth::domain::message

#endif
