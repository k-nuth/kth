// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/version.hpp>

#include <algorithm>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const version::command = "version";
//const bounds message::version::version = { level::minimum, level::maximum };
uint32_t const message::version::version_minimum = level::minimum;
uint32_t const message::version::version_maximum = level::maximum;

version::version(uint32_t value, uint64_t services, uint64_t timestamp, network_address const& address_receiver, network_address const& address_sender, uint64_t nonce, std::string const& user_agent, uint32_t start_height, bool relay)
    : value_(value), services_(services), timestamp_(timestamp), address_receiver_(address_receiver), address_sender_(address_sender), nonce_(nonce), user_agent_(user_agent), start_height_(start_height), relay_(relay) {
}

version::version(uint32_t value, uint64_t services, uint64_t timestamp, network_address const& address_receiver, network_address const& address_sender, uint64_t nonce, std::string&& user_agent, uint32_t start_height, bool relay)
    : value_(value), services_(services), timestamp_(timestamp), address_receiver_(address_receiver), address_sender_(address_sender), nonce_(nonce), user_agent_(std::move(user_agent)), start_height_(start_height), relay_(relay) {
}

bool version::operator==(version const& x) const {
    return (value_ == x.value_) && (services_ == x.services_) &&
           (timestamp_ == x.timestamp_) &&
           (address_receiver_ == x.address_receiver_) &&
           (address_sender_ == x.address_sender_) &&
           (nonce_ == x.nonce_) && (user_agent_ == x.user_agent_) &&
           (start_height_ == x.start_height_) && (relay_ == x.relay_);
}

bool version::operator!=(version const& x) const {
    return !(*this == x);
}

bool version::is_valid() const {
    return value_ != 0
        || services_ != 0
        || timestamp_ != 0
        || address_receiver_.is_valid()
        || address_sender_.is_valid()
        || nonce_ != 0
        || ! user_agent_.empty()
        || start_height_ != 0
        || relay_;
}

void version::reset() {
    value_ = 0;
    services_ = 0;
    timestamp_ = 0;
    address_receiver_.reset();
    address_sender_.reset();
    nonce_ = 0;
    user_agent_.clear();
    user_agent_.shrink_to_fit();
    start_height_ = 0;
    relay_ = false;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<message::version> version::from_data(byte_reader& reader, uint32_t version) {
    auto const value = reader.read_little_endian<uint32_t>();
    if ( ! value) {
        return std::unexpected(value.error());
    }
    auto const services = reader.read_little_endian<uint64_t>();
    if ( ! services) {
        return std::unexpected(services.error());
    }
    auto const timestamp = reader.read_little_endian<uint64_t>();
    if ( ! timestamp) {
        return std::unexpected(timestamp.error());
    }
    auto const address_receiver = network_address::from_data(reader, version, false);
    if ( ! address_receiver) {
        return std::unexpected(address_receiver.error());
    }
    auto const address_sender = network_address::from_data(reader, version, false);
    if ( ! address_sender) {
        return std::unexpected(address_sender.error());
    }
    auto const nonce = reader.read_little_endian<uint64_t>();
    if ( ! nonce) {
        return std::unexpected(nonce.error());
    }
    auto user_agent = reader.read_string();
    if ( ! user_agent) {
        return std::unexpected(user_agent.error());
    }
    auto const start_height = reader.read_little_endian<uint32_t>();
    if ( ! start_height) {
        return std::unexpected(start_height.error());
    }

    auto const peer_bip37 = *value >= level::bip37;
    auto const self_bip37 = version >= level::bip37;

    // The relay field is optional at or above version 70001.
    // But the peer doesn't know our version when it sends its version.
    // This is a bug in the BIP37 design as it forces older peers to adapt to
    // the expansion of the version message, which is a clear compat break.
    // So relay is eabled if either peer is below 70001, it is not set, or
    // peers are at/above 70001 and the field is set.
    auto const relay = peer_bip37 != self_bip37 ||
        reader.is_exhausted() ||
        (self_bip37 && reader.read_byte().value_or(0) != 0);

    return message::version {
        *value,
        *services,
        *timestamp,
        *address_receiver,
        *address_sender,
        *nonce,
        std::move(*user_agent),
        *start_height,
        relay
    };
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk version::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void version::to_data(uint32_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

size_t version::serialized_size(uint32_t version) const {
    auto size =
        sizeof(value_) +
        sizeof(services_) +
        sizeof(timestamp_) +
        address_receiver_.serialized_size(version, false) +
        address_sender_.serialized_size(version, false) +
        sizeof(nonce_) +
        infrastructure::message::variable_uint_size(user_agent_.size()) + user_agent_.size() +
        sizeof(start_height_);

    if (value_ >= level::bip37) {
        size += sizeof(uint8_t);
    }

    return size;
}

uint32_t version::value() const {
    return value_;
}

void version::set_value(uint32_t value) {
    value_ = value;
}

uint64_t version::services() const {
    return services_;
}

void version::set_services(uint64_t services) {
    services_ = services;
}

uint64_t version::timestamp() const {
    return timestamp_;
}

void version::set_timestamp(uint64_t timestamp) {
    timestamp_ = timestamp;
}

network_address& version::address_receiver() {
    return address_receiver_;
}

network_address const& version::address_receiver() const {
    return address_receiver_;
}

void version::set_address_receiver(network_address const& address) {
    address_receiver_ = address;
}

network_address& version::address_sender() {
    return address_sender_;
}

network_address const& version::address_sender() const {
    return address_sender_;
}

void version::set_address_sender(network_address const& address) {
    address_sender_ = address;
}

uint64_t version::nonce() const {
    return nonce_;
}

void version::set_nonce(uint64_t nonce) {
    nonce_ = nonce;
}

std::string& version::user_agent() {
    return user_agent_;
}

std::string const& version::user_agent() const {
    return user_agent_;
}

void version::set_user_agent(std::string const& agent) {
    user_agent_ = agent;
}

void version::set_user_agent(std::string&& agent) {
    user_agent_ = std::move(agent);
}

uint32_t version::start_height() const {
    return start_height_;
}

void version::set_start_height(uint32_t height) {
    start_height_ = height;
}

bool version::relay() const {
    return relay_;
}

void version::set_relay(bool relay) {
    relay_ = relay;
}

} // namespace kth::domain::message
