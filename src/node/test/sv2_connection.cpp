// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <string>

#include <kth/infrastructure.hpp>
#include <kth/node/sv2/connection.hpp>

using namespace kth;
using namespace kth::node::sv2;

// Start Test Suite: sv2 connection tests

TEST_CASE("sv2 SetupConnection round-trips all its fields", "[sv2 connection]") {
    setup_connection const msg{
        /*protocol*/ setup_connection::protocol_template_distribution,
        /*min_version*/ 2u,
        /*max_version*/ 2u,
        /*flags*/ 0x00000001u,
        /*endpoint_host*/ "0.0.0.0",
        /*endpoint_port*/ 3336u,
        /*vendor*/ "knuth",
        /*hardware_version*/ "",
        /*firmware*/ "kth-1.3",
        /*device_id*/ "tp-01"};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes.size() == msg.serialized_size());

    auto const parsed = from_data_chunk<setup_connection>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->protocol == msg.protocol);
    REQUIRE(parsed->min_version == msg.min_version);
    REQUIRE(parsed->max_version == msg.max_version);
    REQUIRE(parsed->flags == msg.flags);
    REQUIRE(parsed->endpoint_host == msg.endpoint_host);
    REQUIRE(parsed->endpoint_port == msg.endpoint_port);
    REQUIRE(parsed->vendor == msg.vendor);
    REQUIRE(parsed->hardware_version == msg.hardware_version);
    REQUIRE(parsed->firmware == msg.firmware);
    REQUIRE(parsed->device_id == msg.device_id);
    REQUIRE(setup_connection::message_type == 0x00);
    REQUIRE(setup_connection::protocol_template_distribution == 2);
}

TEST_CASE("sv2 SetupConnection.Success round-trips and matches the wire layout", "[sv2 connection]") {
    setup_connection_success const msg{/*used_version*/ 0x0002u, /*flags*/ 0x0a0b0c0du};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes == data_chunk{0x02, 0x00, 0x0d, 0x0c, 0x0b, 0x0a});  // U16 LE, U32 LE

    auto const parsed = from_data_chunk<setup_connection_success>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->used_version == msg.used_version);
    REQUIRE(parsed->flags == msg.flags);
    REQUIRE(setup_connection_success::message_type == 0x01);
}

TEST_CASE("sv2 SetupConnection.Error round-trips its flags and reason", "[sv2 connection]") {
    setup_connection_error const msg{/*flags*/ 0x00000002u, "protocol-version-mismatch"};

    auto const bytes = to_data_chunk(msg);
    auto const parsed = from_data_chunk<setup_connection_error>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->flags == msg.flags);
    REQUIRE(parsed->error_code == msg.error_code);
    REQUIRE(setup_connection_error::message_type == 0x02);
}

TEST_CASE("sv2 SetupConnection.Success from_data rejects a short buffer", "[sv2 connection]") {
    data_chunk const short_buffer{0x02, 0x00, 0x0d};  // needs 6
    REQUIRE_FALSE(from_data_chunk<setup_connection_success>(short_buffer).has_value());
}

TEST_CASE("sv2 ChannelEndpointChanged round-trips and matches the wire layout", "[sv2 connection]") {
    channel_endpoint_changed const msg{0x0a0b0c0du};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes == data_chunk{0x0d, 0x0c, 0x0b, 0x0a});  // U32 LE

    auto const parsed = from_data_chunk<channel_endpoint_changed>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->channel_id == msg.channel_id);
    REQUIRE(channel_endpoint_changed::message_type == 0x03);
    REQUIRE(channel_endpoint_changed::channel_message);
    REQUIRE_FALSE(from_data_chunk<channel_endpoint_changed>(data_chunk{0x0d, 0x0c, 0x0b}).has_value());
}

TEST_CASE("sv2 Reconnect round-trips and matches the wire layout", "[sv2 connection]") {
    reconnect const msg{"pool.test", 0x0d0cu};

    auto const bytes = to_data_chunk(msg);
    REQUIRE(bytes == data_chunk{9, 'p', 'o', 'o', 'l', '.', 't', 'e', 's', 't', 0x0c, 0x0d});  // STR0_255, U16 LE

    auto const parsed = from_data_chunk<reconnect>(bytes);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->new_host == msg.new_host);
    REQUIRE(parsed->new_port == msg.new_port);
    REQUIRE(reconnect::message_type == 0x04);

    // Both fields empty: the present host and port.
    REQUIRE(to_data_chunk(reconnect{}) == data_chunk{0x00, 0x00, 0x00});
    REQUIRE_FALSE(from_data_chunk<reconnect>(data_chunk{0x00, 0x01}).has_value());
    REQUIRE_FALSE(from_data_chunk<reconnect>(data_chunk{0x05, 'p', 'o'}).has_value());
}
