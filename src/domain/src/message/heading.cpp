// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/heading.hpp>

#include <kth/domain/constants.hpp>
#include <kth/domain/message/messages.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

size_t heading::maximum_size() {
    // This assumes that the heading doesn't shrink in size.
    return satoshi_fixed_size();
}

// Pre-Witness:
// A maximal inventory is 50,000 entries, the largest valid message.
// Inventory with 50,000 entries is 3 + 36 * 50,000 bytes (1,800,003).
// The variable integer portion is maximum 3 bytes (with a count of 50,000).
// According to protocol documentation get_blocks is limited only by the
// general maximum payload size of 0x02000000 (33,554,432). But this is an
// absurd limit for a message that is properly [10 + log2(height) + 1]. Since
// protocol limits height to 2^32 this is 43. Even with expansion to 2^62
// this is limited to 75. So limit payloads to the max inventory payload size.
// Post-Witness:
// The maximum block size inclusive of witness is greater than 1,800,003, so
// with witness-enabled block size (4,000,000).
size_t heading::maximum_payload_size(uint32_t /*unused*/, uint32_t magic, bool is_chipnet) {
    /*    static constexpr
    size_t vector = sizeof(uint32_t) + hash_size;
    static constexpr
    size_t maximum = 3u + vector * max_inventory;
    static_assert(maximum <= max_size_t, "maximum_payload_size overflow");
*/

    return get_max_payload_size(get_network(magic, is_chipnet));
}

size_t heading::satoshi_fixed_size() {
    return sizeof(uint32_t) + command_size + sizeof(uint32_t) +
           sizeof(uint32_t);
}

heading::heading(uint32_t magic, std::string const& command, uint32_t payload_size, uint32_t checksum)
    : magic_(magic), command_(command), payload_size_(payload_size), checksum_(checksum) {
}

heading::heading(uint32_t magic, std::string&& command, uint32_t payload_size, uint32_t checksum)
    : magic_(magic), command_(std::move(command)), payload_size_(payload_size), checksum_(checksum) {
}

bool heading::operator==(heading const& x) const {
    return (magic_ == x.magic_) && (command_ == x.command_) && (payload_size_ == x.payload_size_) && (checksum_ == x.checksum_);
}

bool heading::operator!=(heading const& x) const {
    return !(*this == x);
}

bool heading::is_valid() const {
    return (magic_ != 0) || (payload_size_ != 0) || (checksum_ != 0) || !command_.empty();
}

void heading::reset() {
    magic_ = 0;
    command_.clear();
    command_.shrink_to_fit();
    payload_size_ = 0;
    checksum_ = 0;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<heading> heading::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const magic = reader.read_little_endian<uint32_t>();
    if ( ! magic) {
        return std::unexpected(magic.error());
    }
    auto const command = reader.read_string(command_size);
    if ( ! command) {
        return std::unexpected(command.error());
    }
    auto const payload_size = reader.read_little_endian<uint32_t>();
    if ( ! payload_size) {
        return std::unexpected(payload_size.error());
    }
    auto const checksum = reader.read_little_endian<uint32_t>();
    if ( ! checksum) {
        return std::unexpected(checksum.error());
    }
    return heading(*magic, std::move(*command), *payload_size, *checksum);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk heading::to_data() const {
    data_chunk data;
    auto const size = satoshi_fixed_size();
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void heading::to_data(data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(sink_w);
}

message_type heading::type() const {
    // TODO(legacy): convert to static map.
    if (command_ == address::command) return message_type::address;
    if (command_ == alert::command) return message_type::alert;
    if (command_ == block_transactions::command) return message_type::block_transactions;
    if (command_ == block::command) return message_type::block;
    if (command_ == compact_block::command) return message_type::compact_block;
    if (command_ == double_spend_proof::command) return message_type::double_spend_proof;
    if (command_ == fee_filter::command) return message_type::fee_filter;
    if (command_ == filter_add::command) return message_type::filter_add;
    if (command_ == filter_clear::command) return message_type::filter_clear;
    if (command_ == filter_load::command) return message_type::filter_load;
    if (command_ == get_address::command) return message_type::get_address;
    if (command_ == get_block_transactions::command) return message_type::get_block_transactions;
    if (command_ == get_blocks::command) return message_type::get_blocks;
    if (command_ == get_data::command) return message_type::get_data;
    if (command_ == get_headers::command) return message_type::get_headers;
    if (command_ == headers::command) return message_type::headers;
    if (command_ == inventory::command) return message_type::inventory;
    if (command_ == memory_pool::command) return message_type::memory_pool;
    if (command_ == merkle_block::command) return message_type::merkle_block;
    if (command_ == not_found::command) return message_type::not_found;
    if (command_ == ping::command) return message_type::ping;
    if (command_ == pong::command) return message_type::pong;
    if (command_ == reject::command) return message_type::reject;
    if (command_ == send_compact::command) return message_type::send_compact;
    if (command_ == send_headers::command) return message_type::send_headers;
    if (command_ == transaction::command) return message_type::transaction;
    if (command_ == verack::command) return message_type::verack;
    if (command_ == version::command) return message_type::version;
    if (command_ == xverack::command) return message_type::xverack;
    if (command_ == xversion::command) return message_type::xversion;

    return message_type::unknown;
}

uint32_t heading::magic() const {
    return magic_;
}

void heading::set_magic(uint32_t value) {
    magic_ = value;
}

std::string& heading::command() {
    return command_;
}

std::string const& heading::command() const {
    return command_;
}

void heading::set_command(std::string const& value) {
    command_ = value;
}

void heading::set_command(std::string&& value) {
    command_ = std::move(value);
}

uint32_t heading::payload_size() const {
    return payload_size_;
}

void heading::set_payload_size(uint32_t value) {
    payload_size_ = value;
}

uint32_t heading::checksum() const {
    return checksum_;
}

void heading::set_checksum(uint32_t value) {
    checksum_ = value;
}

} // namespace kth::domain::message
