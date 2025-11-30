// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_ANNOUNCE_VERSION_HPP
#define KTH_DOMAIN_MESSAGE_ANNOUNCE_VERSION_HPP

#include <cstdint>
#include <istream>
#include <memory>
#include <string>

#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

using namespace kth::infrastructure::message;

// The checksum is ignored by the version command.
struct KD_API version {
    using ptr = std::shared_ptr<version>;
    using const_ptr = std::shared_ptr<const version>;

    enum level : uint32_t {
        // compact blocks protocol FIX
        bip152_fix = 70015,  //TODO(fernando): See how to name this, because there is no BIP for that...

        // compact blocks protocol
        bip152 = 70014,

        // fee_filter
        bip133 = 70013,

        // send_headers
        bip130 = 70012,

        // node_bloom service bit
        bip111 = 70011,

        // node_utxo service bit (draft)
        bip64 = 70004,

        // reject (satoshi node writes version.relay starting here)
        bip61 = 70002,

        // filters, merkle_block, not_found, version.relay
        bip37 = 70001,

        // memory_pool
        bip35 = 60002,

        // ping.nonce, pong
        bip31 = 60001,

        // Don't request blocks from nodes of versions 32000-32400.
        no_blocks_end = 32400,

        // Don't request blocks from nodes of versions 32000-32400.
        no_blocks_start = 32000,

    // This preceded the BIP system.
#if defined(KTH_CURRENCY_LTC)
        headers = 70002,
#else
        headers = 31800,
#endif

        // We require at least this of peers, address.time fields.
        minimum = 31402,

        // We support at most this internally (bound to settings default).
        maximum = bip152_fix,

        // Used to generate canonical size required by consensus checks.
        canonical = 0
    };

    enum service : uint64_t {
        // The node exposes no services.
        none = 0,

        // The node is capable of serving the block chain (full node).
        node_network = (1U << 0),

        // Requires version.value >= level::bip64 (BIP64 is draft only).
        // The node is capable of responding to the getutxo protocol request.
        node_utxo = (1U << 1),

        // Requires version.value >= level::bip111

        // The node is capable and willing to handle bloom-filtered connections.
        node_bloom = (1U << 2),

        // // Independent of network protocol level.
        // // The node is capable of responding to witness inventory requests.
        // node_witness = (1U << 3),

#if defined(KTH_CURRENCY_BCH)
        node_network_cash = (1U << 5)  //TODO(kth): check what happens with node_network (or node_network_cash)
#endif                                //KTH_CURRENCY_BCH
    };

    version() = default;
    version(uint32_t value, uint64_t services, uint64_t timestamp, network_address const& address_receiver, network_address const& address_sender, uint64_t nonce, std::string const& user_agent, uint32_t start_height, bool relay);
    version(uint32_t value, uint64_t services, uint64_t timestamp, network_address const& address_receiver, network_address const& address_sender, uint64_t nonce, std::string&& user_agent, uint32_t start_height, bool relay);

    bool operator==(version const& x) const;
    bool operator!=(version const& x) const;

    [[nodiscard]]
    uint32_t value() const;

    void set_value(uint32_t value);

    [[nodiscard]]
    uint64_t services() const;

    void set_services(uint64_t services);

    [[nodiscard]]
    uint64_t timestamp() const;

    void set_timestamp(uint64_t timestamp);

    network_address& address_receiver();

    [[nodiscard]]
    network_address const& address_receiver() const;

    void set_address_receiver(network_address const& address);
    network_address& address_sender();

    [[nodiscard]]
    network_address const& address_sender() const;

    void set_address_sender(network_address const& address);

    [[nodiscard]]
    uint64_t nonce() const;

    void set_nonce(uint64_t nonce);

    std::string& user_agent();

    [[nodiscard]]
    std::string const& user_agent() const;

    void set_user_agent(std::string const& agent);
    void set_user_agent(std::string&& agent);

    [[nodiscard]]
    uint32_t start_height() const;

    void set_start_height(uint32_t height);

    // version >= 70001
    [[nodiscard]]
    bool relay() const;

    void set_relay(bool relay);

    static
    expect<message::version> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    data_chunk to_data(uint32_t version) const;

    void to_data(uint32_t version, data_sink& stream) const;

    template <typename W>
    void to_data(uint32_t version, W& sink) const {
        sink.write_4_bytes_little_endian(value_);
        auto const effective_version = std::min(version, value_);
        sink.write_8_bytes_little_endian(services_);
        sink.write_8_bytes_little_endian(timestamp_);
        address_receiver_.to_data(version, sink, false);
        address_sender_.to_data(version, sink, false);
        sink.write_8_bytes_little_endian(nonce_);
        sink.write_string(user_agent_);
        sink.write_4_bytes_little_endian(start_height_);

        if (effective_version >= level::bip37) {
            sink.write_byte(relay_ ? 1 : 0);
        }
    }

    //void to_data(uint32_t version, writer& sink) const;
    [[nodiscard]]
    bool is_valid() const;

    void reset();

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

    static
    std::string const command;

    //static
    //const bounds version;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;


private:
    uint32_t value_{0};
    uint64_t services_{0};
    uint64_t timestamp_{0};
    network_address address_receiver_;
    network_address address_sender_;
    uint64_t nonce_{0};
    std::string user_agent_;
    uint32_t start_height_{0};

    // version >= 70001
    bool relay_{false};
};

} // namespace kth::domain::message

#endif
