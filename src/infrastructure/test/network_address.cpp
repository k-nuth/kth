// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure.hpp>

using namespace kth;
using namespace kth::infrastructure;

using message::network_address;

namesspace {
//This is defined in Domain <kth/domain/message/version.hpp>
constexpr uint32_t version_level_minimum = 31402;

bool equal(network_address const& x, network_address const& y, bool with_timestamp) {
    bool matches_timestamp = with_timestamp ? (x.timestamp() == y.timestamp()) : true;
    return matches_timestamp && (x == y);
}

} // anonymous namespace

// Start Test Suite: network address tests

TEST_CASE("network address  constructor 1  always  invalid", "[network address tests]") {
    network_address instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("network address  constructor 2  always  equals params", "[network address tests]") {
    constexpr uint32_t timestamp = 734678u;
    constexpr uint64_t services = 5357534u;
    constexpr uint16_t port = 123u;
    constexpr message::ip_address ip = "127544abcdefa7b6d3e91486c57000aa"_base16;

    network_address const instance {
        timestamp, 
        services, 
        ip, 
        port
    };

    REQUIRE(instance.is_valid());
    REQUIRE(ip == instance.ip());
    REQUIRE(port == instance.port());
    REQUIRE(services == instance.services());
    REQUIRE(timestamp == instance.timestamp());
}

TEST_CASE("network address  constructor 3  always  equals params", "[network address tests]") {
    constexpr uint32_t timestamp = 734678u;
    constexpr uint64_t services = 5357534u;
    constexpr uint16_t port = 123u;

    network_address const instance {
        timestamp, 
        services, 
        "127544abcdefa7b6d3e91486c57000aa"_base16, 
        port
    };

    REQUIRE(instance.is_valid());
}

TEST_CASE("network address  constructor 4  always  equals params", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    REQUIRE(expected.is_valid());

    network_address const instance(expected);
    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("network address  constructor 5  always  equals params", "[network address tests]") {
    network_address expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    REQUIRE(expected.is_valid());

    network_address const instance(std::move(expected));
    REQUIRE(instance.is_valid());
}

TEST_CASE("network address from data insufficient bytes  failure", "[network address tests]") {
    data_chunk const raw{1};
    byte_reader reader(raw);

    auto const result = network_address::from_data(reader, version_level_minimum, false);
    REQUIRE( ! result);
}

TEST_CASE("network address  from data 1  without timestamp  success", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    auto const data = expected.to_data(version_level_minimum, false);
    byte_reader reader(data);
    auto const result = network_address::from_data(reader, version_level_minimum, false);

    REQUIRE(result);
    REQUIRE(equal(expected, *result, false));
    REQUIRE(data.size() == result->serialized_size(version_level_minimum, false));
    REQUIRE(expected.serialized_size(version_level_minimum, false) == result->serialized_size(version_level_minimum, false));
}

TEST_CASE("network address  from data 2  without timestamp  success", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    auto const data = expected.to_data(version_level_minimum, false);
    byte_reader reader(data);
    auto const result = network_address::from_data(reader, version_level_minimum, false);

    REQUIRE(result);
    REQUIRE(equal(expected, *result, false));
    REQUIRE(data.size() == result->serialized_size(version_level_minimum, false));
    REQUIRE(expected.serialized_size(version_level_minimum, false) == result->serialized_size(version_level_minimum, false));
}

TEST_CASE("network address  from data 3  without timestamp  success", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    auto const data = expected.to_data(version_level_minimum, false);
    byte_reader reader(data);
    auto const result = network_address::from_data(reader, version_level_minimum, false);

    REQUIRE(result);
    REQUIRE(equal(expected, *result, false));
    REQUIRE(data.size() == result->serialized_size(version_level_minimum, false));
    REQUIRE(expected.serialized_size(version_level_minimum, false) == result->serialized_size(version_level_minimum, false));
}

TEST_CASE("network address  from data 1  with timestamp  success", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    auto const data = expected.to_data(version_level_minimum, true);
    byte_reader reader(data);
    auto const result = network_address::from_data(reader, version_level_minimum, true);

    REQUIRE(result);
    REQUIRE(equal(expected, *result, true));
    REQUIRE(data.size() == result->serialized_size(version_level_minimum, true));
    REQUIRE(expected.serialized_size(version_level_minimum, true) == result->serialized_size(version_level_minimum, true));
}

TEST_CASE("network address  from data 2  with timestamp  success", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    auto const data = expected.to_data(version_level_minimum, true);
    byte_reader reader(data);
    auto const result = network_address::from_data(reader, version_level_minimum, true);

    REQUIRE(result);
    REQUIRE(equal(expected, *result, true));
    REQUIRE(data.size() == result->serialized_size(version_level_minimum, true));
    REQUIRE(expected.serialized_size(version_level_minimum, true) == result->serialized_size(version_level_minimum, true));
}

TEST_CASE("network address  from data 3  with timestamp  success", "[network address tests]") {
    network_address const expected {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    auto const data = expected.to_data(version_level_minimum, true);
    byte_reader reader(data);
    auto const result = network_address::from_data(reader, version_level_minimum, true);

    REQUIRE(result);
    REQUIRE(equal(expected, *result, true));
    REQUIRE(data.size() == result->serialized_size(version_level_minimum, true));
    REQUIRE(expected.serialized_size(version_level_minimum, true) == result->serialized_size(version_level_minimum, true));
}

TEST_CASE("network address  timestamp accessor  always  returns initialized value", "[network address tests]") {
    constexpr uint32_t timestamp = 734678u;
    network_address const instance {
        timestamp,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    REQUIRE(timestamp == instance.timestamp());
}

TEST_CASE("network address  timestamp setter  roundtrip  success", "[network address tests]") {
    constexpr uint32_t timestamp = 734678u;
    network_address instance;
    REQUIRE(timestamp != instance.timestamp());
    instance.set_timestamp(timestamp);
    REQUIRE(timestamp == instance.timestamp());
}

TEST_CASE("network address  services accessor  always  returns initialized value", "[network address tests]") {
    constexpr uint32_t services = 5357534u;
    network_address instance {
        734678u,
        services,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        123u
    };

    REQUIRE(services == instance.services());
}

TEST_CASE("network address  services setter  roundtrip  success", "[network address tests]") {
    constexpr uint64_t services = 6842368u;
    network_address instance;
    REQUIRE(services != instance.services());
    instance.set_services(services);
    REQUIRE(services == instance.services());
}

TEST_CASE("network address  ip accessor  always  returns initialized value", "[network address tests]") {
    constexpr message::ip_address ip = "127544abcdefa7b6d3e91486c57000aa"_base16;

    network_address const instance {
        734678u,
        5357534u,
        ip,
        123u
    };

    REQUIRE(ip == instance.ip());
}

TEST_CASE("network address  ip setter 1  roundtrip  success", "[network address tests]") {
    constexpr message::ip_address ip = "127544abcdefa7b6d3e91486c57000aa"_base16;

    network_address instance;
    REQUIRE(ip != instance.ip());
    instance.set_ip(ip);
    REQUIRE(ip == instance.ip());
}

TEST_CASE("network address  ip setter 2  roundtrip  success", "[network address tests]") {
    constexpr message::ip_address ip = "127544abcdefa7b6d3e91486c57000aa"_base16;

    network_address instance;
    REQUIRE(ip != instance.ip());
    instance.set_ip("127544abcdefa7b6d3e91486c57000aa"_base16);
    REQUIRE(ip == instance.ip());
}

TEST_CASE("network address  port accessor  always  returns initialized value", "[network address tests]") {
    constexpr uint16_t port = 123u;
    network_address const instance {
        734678u,
        5357534u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        port
    };

    REQUIRE(port == instance.port());
}

TEST_CASE("network address  port setter  roundtrip  success", "[network address tests]") {
    constexpr uint16_t port = 853u;
    network_address instance;
    REQUIRE(port != instance.port());
    instance.set_port(port);
    REQUIRE(port == instance.port());
}

TEST_CASE("network address  operator assign equals 1  always  matches equivalent", "[network address tests]") {
    network_address const value {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    REQUIRE(value.is_valid());

    network_address instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
}

TEST_CASE("network address  operator assign equals 2  always  matches equivalent", "[network address tests]") {
    network_address const value {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    REQUIRE(value.is_valid());

    network_address instance;
    REQUIRE( ! instance.is_valid());

    instance = value;
    REQUIRE(instance.is_valid());
    REQUIRE(value == instance);
}

TEST_CASE("network address  operator boolean equals  duplicates  returns true", "[network address tests]") {
    network_address const expected {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    network_address const instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("network address  operator boolean equals  differs timestamp  returns true", "[network address tests]") {
    network_address const expected {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    network_address const instance(643u, expected.services(), expected.ip(), expected.port());
    REQUIRE(instance == expected);
}

TEST_CASE("network address  operator boolean equals  differs  returns false", "[network address tests]") {
    network_address const expected {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    network_address const instance;
    REQUIRE(instance != expected);
}

TEST_CASE("network address  operator boolean not equals  duplicates  returns false", "[network address tests]") {
    network_address const expected {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    network_address const instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("network address  operator boolean not equals  differs timestamp  returns false", "[network address tests]") {
    network_address const expected {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    network_address const instance(643u, expected.services(), expected.ip(), expected.port());
    REQUIRE(instance == expected);
}

TEST_CASE("network address  operator boolean not equals  differs  returns true", "[network address tests]") {
    network_address const expected {
        14356u,
        54676843u,
        "127544abcdefa7b6d3e91486c57000aa"_base16,
        1500u
    };

    network_address const instance;
    REQUIRE(instance != expected);
}

// End Test Suite
