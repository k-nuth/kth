// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_HEADING_HPP
#define KTH_DOMAIN_MESSAGE_HEADING_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>

#include <boost/array.hpp>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/checksum.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

enum class message_type {
    unknown,
    address,
    alert,
    block,
    block_transactions,
    compact_block,
    double_spend_proof,
    fee_filter,
    filter_add,
    filter_clear,
    filter_load,
    get_address,
    get_block_transactions,
    get_blocks,
    get_data,
    get_headers,
    headers,
    inventory,
    memory_pool,
    merkle_block,
    not_found,
    ping,
    pong,
    reject,
    send_compact,
    send_headers,
    transaction,
    verack,
    version,
    xverack,
    xversion
};

struct KD_API heading {
    static
    size_t maximum_size();

    static
    size_t maximum_payload_size(uint32_t version, uint32_t magic, bool is_chipnet);

    static
    size_t satoshi_fixed_size();

    heading() = default;
    heading(uint32_t magic, std::string const& command, uint32_t payload_size, uint32_t checksum);
    heading(uint32_t magic, std::string&& command, uint32_t payload_size, uint32_t checksum);

    bool operator==(heading const& x) const;
    bool operator!=(heading const& x) const;

    [[nodiscard]]
    uint32_t magic() const;

    void set_magic(uint32_t value);

    std::string& command();

    [[nodiscard]]
    std::string const& command() const;

    void set_command(std::string const& value);
    void set_command(std::string&& value);

    [[nodiscard]]
    uint32_t payload_size() const;

    void set_payload_size(uint32_t value);

    [[nodiscard]]
    uint32_t checksum() const;

    void set_checksum(uint32_t value);

    [[nodiscard]]
    message_type type() const;

    static
    expect<heading> from_data(byte_reader& reader, uint32_t /*version*/);

    [[nodiscard]]
    data_chunk to_data() const;

    void to_data(data_sink& stream) const;

    template <typename W>
    void to_data(W& sink) const {
        sink.write_4_bytes_little_endian(magic_);
        sink.write_string(command_, command_size);
        sink.write_4_bytes_little_endian(payload_size_);
        sink.write_4_bytes_little_endian(checksum_);
    }

    //void to_data(writer& sink) const;
    [[nodiscard]]
    bool is_valid() const;

    void reset();


private:
    uint32_t magic_{0};
    std::string command_;
    uint32_t payload_size_{0};
    uint32_t checksum_{0};
};

} // namespace kth::domain::message

#endif
