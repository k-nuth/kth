// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_CONNECTION_HPP
#define KTH_NODE_SV2_CONNECTION_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

// Stratum V2 common connection-setup messages (sv2-spec 03). Every SV2
// sub-protocol opens with these before its own messages; the Template
// Distribution provider negotiates one with protocol == template_distribution.
// channel_msg is unset and extension_type is 0 for all three.

namespace kth::node::sv2 {

// SetupConnection (0x00, Client -> Server): open a connection for one
// sub-protocol and negotiate the version range and feature flags.
struct setup_connection {
    static constexpr uint8_t message_type = 0x00;

    // protocol field values.
    static constexpr uint8_t protocol_mining = 0;
    static constexpr uint8_t protocol_job_declaration = 1;
    static constexpr uint8_t protocol_template_distribution = 2;

    uint8_t protocol = 0;
    uint16_t min_version = 0;
    uint16_t max_version = 0;
    uint32_t flags = 0;
    std::string endpoint_host;      // STR0_255
    uint16_t endpoint_port = 0;
    std::string vendor;             // STR0_255
    std::string hardware_version;   // STR0_255
    std::string firmware;           // STR0_255
    std::string device_id;          // STR0_255

    [[nodiscard]] size_t serialized_size() const {
        return 1 + 2 + 2 + 4
            + (1 + endpoint_host.size())
            + 2
            + (1 + vendor.size())
            + (1 + hardware_version.size())
            + (1 + firmware.size())
            + (1 + device_id.size());
    }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<setup_connection> from_data(byte_reader& source);
};

// SetupConnection.Success (0x01, Server -> Client): the negotiated version and
// the flags the server accepted.
struct setup_connection_success {
    static constexpr uint8_t message_type = 0x01;

    uint16_t used_version = 0;
    uint32_t flags = 0;

    [[nodiscard]] size_t serialized_size() const { return 2 + 4; }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<setup_connection_success> from_data(byte_reader& source);
};

// SetupConnection.Error (0x02, Server -> Client): version negotiation or a
// required feature failed.
struct setup_connection_error {
    static constexpr uint8_t message_type = 0x02;

    uint32_t flags = 0;
    std::string error_code;         // STR0_255

    [[nodiscard]] size_t serialized_size() const { return 4 + 1 + error_code.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<setup_connection_error> from_data(byte_reader& source);
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_CONNECTION_HPP
