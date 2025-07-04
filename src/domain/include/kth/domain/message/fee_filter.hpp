// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_FEE_FILTER_HPP
#define KTH_DOMAIN_MESSAGE_FEE_FILTER_HPP

#include <cstdint>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

class KD_API fee_filter {
public:
    using ptr = std::shared_ptr<fee_filter>;
    using const_ptr = std::shared_ptr<const fee_filter>;

    static constexpr
    size_t satoshi_fixed_size(uint32_t version);

    fee_filter() = default;
    fee_filter(uint64_t minimum);

    bool operator==(fee_filter const& x) const;
    bool operator!=(fee_filter const& x) const;

    [[nodiscard]]
    uint64_t minimum_fee() const;

    void set_minimum_fee(uint64_t value);

    static
    expect<fee_filter> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W& sink) const {
        sink.write_8_bytes_little_endian(minimum_fee_);
    }

    //void to_data(uint32_t version, writer& sink) const;

    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;


    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

protected:
    fee_filter(uint64_t minimum, bool insufficient_version);

private:
    uint64_t minimum_fee_{0};
    bool insufficient_version_{true};
};

} // namespace kth::domain::message

#endif
