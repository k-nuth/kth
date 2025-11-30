// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_SEND_COMPACT_BLOCKS_HPP
#define KTH_DOMAIN_MESSAGE_SEND_COMPACT_BLOCKS_HPP

#include <istream>
#include <memory>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API send_compact {
    using ptr = std::shared_ptr<send_compact>;
    using const_ptr = std::shared_ptr<const send_compact>;

    static
    size_t satoshi_fixed_size(uint32_t version);

    send_compact() = default;
    send_compact(bool high_bandwidth_mode, uint64_t version);

    bool operator==(send_compact const& x) const;
    bool operator!=(send_compact const& x) const;


    [[nodiscard]]
    bool high_bandwidth_mode() const;

    void set_high_bandwidth_mode(bool mode);

    [[nodiscard]]
    uint64_t version() const;

    void set_version(uint64_t version);

    static
    expect<send_compact> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t /*version*/, W& sink) const {
        sink.write_byte(static_cast<uint8_t>(high_bandwidth_mode_));
        sink.write_8_bytes_little_endian(this->version_);
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


private:
    bool high_bandwidth_mode_{false};
    uint64_t version_{0};
};

} // namespace kth::domain::message

#endif
