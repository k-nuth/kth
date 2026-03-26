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
        // Feature negotiation before verack (BIP155 sendaddrv2 support)
        // BCHN: FEATURE_NEGOTIATION_BEFORE_VERACK_VERSION
        feature_negotiation = 70016,

        // Not banning for invalid compact blocks
        // BCHN: INVALID_CB_NO_BAN_VERSION
        invalid_cb_no_ban = 70015,

        // Short-id-based block download (compact blocks, BIP152)
        // BCHN: SHORT_IDS_BLOCKS_VERSION
        bip152 = 70014,

        // Fee filter (BIP133)
        // BCHN: FEEFILTER_VERSION
        bip133 = 70013,

        // Send headers (BIP130)
        // BCHN: SENDHEADERS_VERSION
        bip130 = 70012,

        // Node bloom service bit (BIP111)
        // BCHN: NO_BLOOM_VERSION
        bip111 = 70011,

        // Node UTXO service bit (BIP64, draft only)
        bip64 = 70004,

        // Reject message (BIP61, deprecated)
        bip61 = 70002,

        // Bloom filters, merkle_block, not_found, version.relay (BIP37)
        bip37 = 70001,

        // Memory pool (BIP35)
        bip35 = 60002,

        // Ping nonce, pong (BIP31) - first version with BIP31 support
        // Note: BCHN defines BIP0031_VERSION = 60000 and uses `> 60000`
        // We define bip31 = 60001 and use `>= bip31` (equivalent logic)
        bip31 = 60001,

        // Don't request blocks from nodes of versions 32000-32400.
        no_blocks_end = 32400,

        // Don't request blocks from nodes of versions 32000-32400.
        no_blocks_start = 32000,

        // Minimum version for headers-first sync
        // BCHN: MIN_PEER_PROTO_VERSION
#if defined(KTH_CURRENCY_LTC)
        headers = 70002,
#else
        headers = 31800,
#endif

        // We require at least this of peers, address.time fields.
        minimum = 31402,

        // Initial protocol version before negotiation
        // BCHN: INIT_PROTO_VERSION
        initial = 209,

        // We support at most this internally (bound to settings default).
        // BCHN: PROTOCOL_VERSION
        maximum = feature_negotiation,

        // Used to generate canonical size required by consensus checks.
        canonical = 0
    };

    enum service : uint64_t {
        // Nothing
        none = 0,

        // NODE_NETWORK means that the node is capable of serving the complete block
        // chain. It is currently set by all non-pruned nodes, and is unset by SPV
        // clients or other light clients.
        node_network = (1U << 0),

        // NODE_GETUTXO means the node is capable of responding to the getutxo
        // protocol request. See BIP64 for details on how this is implemented.
        node_getutxo = (1U << 1),

        // NODE_BLOOM means the node is capable and willing to handle bloom-filtered
        // connections. Nodes used to support this by default, without advertising
        // this bit, but no longer do as of protocol version 70011 (= NO_BLOOM_VERSION)
        node_bloom = (1U << 2),

        // Bit 3 is reserved (was NODE_WITNESS in BTC)

#if defined(KTH_CURRENCY_BCH)
        // NODE_XTHIN means the node supports Xtreme Thinblocks. If this is turned
        // off then the node will not service nor make xthin requests.
        node_xthin = (1U << 4),

        // NODE_BITCOIN_CASH means the node supports Bitcoin Cash and the
        // associated consensus rule changes.
        // This service bit is intended to be used prior until some time after the
        // UAHF activation when the Bitcoin Cash network has adequately separated.
        // TODO: remove (free up) the NODE_BITCOIN_CASH service bit once no longer needed.
        node_bitcoin_cash = (1U << 5),

        // NODE_GRAPHENE means the node supports Graphene blocks.
        // If this is turned off then the node will not service graphene requests
        // nor make graphene requests.
        node_graphene = (1U << 6),

        // NODE_CF means that the node supports BIP 157/158 style
        // compact filters on block data.
        node_compact_filters = (1U << 8),
#endif  // KTH_CURRENCY_BCH

        // NODE_NETWORK_LIMITED means the same as NODE_NETWORK with the limitation
        // of only serving the last 288 (2 day) blocks.
        // See BIP159 for details on how this is implemented.
        node_network_limited = (1U << 10),

#if defined(KTH_CURRENCY_BCH)
        // NODE_EXTVERSION indicates if node is using extversion.
        node_extversion = (1U << 11),
#endif  // KTH_CURRENCY_BCH

        // The last non-experimental service bit, helper for looping over the flags.
        // Bits 24-31 are reserved for temporary experiments.
        node_last_non_experimental = (1U << 23)
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
