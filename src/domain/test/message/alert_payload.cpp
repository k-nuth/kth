// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: alert payload tests

TEST_CASE("alert payload  constructor 1  always invalid", "[alert payload]") {
    message::alert_payload instance;
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("alert payload  constructor 2  always  equals params", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, set_cancel, min_version, max_version, set_sub_version,
                                    priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(relay_until == instance.relay_until());
    REQUIRE(expiration == instance.expiration());
    REQUIRE(id == instance.id());
    REQUIRE(cancel == instance.cancel());
    REQUIRE(set_cancel == instance.set_cancel());
    REQUIRE(min_version == instance.min_version());
    REQUIRE(max_version == instance.max_version());
    REQUIRE(set_sub_version == instance.set_sub_version());
    REQUIRE(priority == instance.priority());
    REQUIRE(comment == instance.comment());
    REQUIRE(status_bar == instance.status_bar());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload  constructor 3  always  equals params", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    auto dup_set_cancel = set_cancel;
    auto dup_set_sub_version = set_sub_version;
    auto dup_comment = comment;
    auto dup_status_bar = status_bar;
    auto dup_reserved = reserved;

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, std::move(dup_set_cancel), min_version, max_version,
                                    std::move(dup_set_sub_version), priority, std::move(dup_comment),
                                    std::move(dup_status_bar), std::move(dup_reserved));

    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(relay_until == instance.relay_until());
    REQUIRE(expiration == instance.expiration());
    REQUIRE(id == instance.id());
    REQUIRE(cancel == instance.cancel());
    REQUIRE(set_cancel == instance.set_cancel());
    REQUIRE(min_version == instance.min_version());
    REQUIRE(max_version == instance.max_version());
    REQUIRE(set_sub_version == instance.set_sub_version());
    REQUIRE(priority == instance.priority());
    REQUIRE(comment == instance.comment());
    REQUIRE(status_bar == instance.status_bar());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload  constructor 4  always  equals params", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload value(version, relay_until, expiration, id,
                                 cancel, set_cancel, min_version, max_version, set_sub_version,
                                 priority, comment, status_bar, reserved);

    message::alert_payload instance(value);

    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(relay_until == instance.relay_until());
    REQUIRE(expiration == instance.expiration());
    REQUIRE(id == instance.id());
    REQUIRE(cancel == instance.cancel());
    REQUIRE(set_cancel == instance.set_cancel());
    REQUIRE(min_version == instance.min_version());
    REQUIRE(max_version == instance.max_version());
    REQUIRE(set_sub_version == instance.set_sub_version());
    REQUIRE(priority == instance.priority());
    REQUIRE(comment == instance.comment());
    REQUIRE(status_bar == instance.status_bar());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload  constructor 5  always  equals params", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload value(version, relay_until, expiration, id,
                                 cancel, set_cancel, min_version, max_version, set_sub_version,
                                 priority, comment, status_bar, reserved);

    message::alert_payload instance(std::move(value));

    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(relay_until == instance.relay_until());
    REQUIRE(expiration == instance.expiration());
    REQUIRE(id == instance.id());
    REQUIRE(cancel == instance.cancel());
    REQUIRE(set_cancel == instance.set_cancel());
    REQUIRE(min_version == instance.min_version());
    REQUIRE(max_version == instance.max_version());
    REQUIRE(set_sub_version == instance.set_sub_version());
    REQUIRE(priority == instance.priority());
    REQUIRE(comment == instance.comment());
    REQUIRE(status_bar == instance.status_bar());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload from data insufficient bytes  failure", "[alert payload]") {
    data_chunk raw{0xab, 0x11};
    message::alert_payload instance;

    byte_reader reader(raw);
    auto result = message::alert_payload::from_data(reader, message::version::level::minimum);
    REQUIRE( ! result);
    REQUIRE( ! instance.is_valid());
}

TEST_CASE("alert payload from data wiki sample test  success", "[alert payload]") {
    data_chunk const raw{
        0x01, 0x00, 0x00, 0x00, 0x37, 0x66, 0x40, 0x4f, 0x00, 0x00,
        0x00, 0x00, 0xb3, 0x05, 0x43, 0x4f, 0x00, 0x00, 0x00, 0x00,
        0xf2, 0x03, 0x00, 0x00, 0xf1, 0x03, 0x00, 0x00, 0x00, 0x10,
        0x27, 0x00, 0x00, 0x48, 0xee, 0x00, 0x00, 0x00, 0x64, 0x00,
        0x00, 0x00, 0x00, 0x46, 0x53, 0x65, 0x65, 0x20, 0x62, 0x69,
        0x74, 0x63, 0x6f, 0x69, 0x6e, 0x2e, 0x6f, 0x72, 0x67, 0x2f,
        0x66, 0x65, 0x62, 0x32, 0x30, 0x20, 0x69, 0x66, 0x20, 0x79,
        0x6f, 0x75, 0x20, 0x68, 0x61, 0x76, 0x65, 0x20, 0x74, 0x72,
        0x6f, 0x75, 0x62, 0x6c, 0x65, 0x20, 0x63, 0x6f, 0x6e, 0x6e,
        0x65, 0x63, 0x74, 0x69, 0x6e, 0x67, 0x20, 0x61, 0x66, 0x74,
        0x65, 0x72, 0x20, 0x32, 0x30, 0x20, 0x46, 0x65, 0x62, 0x72,
        0x75, 0x61, 0x72, 0x79, 0x00};

    const message::alert_payload expected{
        1,
        1329620535,
        1329792435,
        1010,
        1009,
        std::vector<uint32_t>{},
        10000,
        61000,
        std::vector<std::string>{},
        100,
        "",
        "See bitcoin.org/feb20 if you have trouble connecting after 20 February",
        ""};

    byte_reader reader(raw);
    auto const result_exp = message::alert_payload::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(raw.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(result == expected);

    auto const data = expected.to_data(message::version::level::minimum);

    REQUIRE(raw.size() == data.size());
    REQUIRE(data.size() == expected.serialized_size(message::version::level::minimum));
}

TEST_CASE("alert payload from data roundtrip  success", "[alert payload]") {
    message::alert_payload expected{
        5,
        105169,
        723544,
        1779,
        1678,
        {10, 25256, 37, 98485, 250},
        75612,
        81354,
        {"alpha", "beta", "gamma", "delta"},
        781,
        "My Comment",
        "My Status Bar",
        "RESERVED?"};

    auto const data = expected.to_data(message::version::level::minimum);
    byte_reader reader(data);
    auto const result_exp = message::alert_payload::from_data(reader, message::version::level::minimum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(result.is_valid());
    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::minimum));
    REQUIRE(expected.serialized_size(message::version::level::minimum) == result.serialized_size(message::version::level::minimum));
}



TEST_CASE("alert payload  version  roundtrip  success", "[alert payload]") {
    uint32_t value = 1234u;
    message::alert_payload instance;
    REQUIRE(instance.version() != value);
    instance.set_version(value);
    REQUIRE(value == instance.version());
}

TEST_CASE("alert payload  relay until  roundtrip  success", "[alert payload]") {
    uint64_t value = 5121234u;
    message::alert_payload instance;
    REQUIRE(instance.relay_until() != value);
    instance.set_relay_until(value);
    REQUIRE(value == instance.relay_until());
}

TEST_CASE("alert payload  expiration  roundtrip  success", "[alert payload]") {
    uint64_t value = 5121234u;
    message::alert_payload instance;
    REQUIRE(instance.expiration() != value);
    instance.set_expiration(value);
    REQUIRE(value == instance.expiration());
}

TEST_CASE("alert payload  id  roundtrip  success", "[alert payload]") {
    uint32_t value = 68215u;
    message::alert_payload instance;
    REQUIRE(instance.id() != value);
    instance.set_id(value);
    REQUIRE(value == instance.id());
}

TEST_CASE("alert payload  cancel  roundtrip  success", "[alert payload]") {
    uint32_t value = 68215u;
    message::alert_payload instance;
    REQUIRE(instance.cancel() != value);
    instance.set_cancel(value);
    REQUIRE(value == instance.cancel());
}

TEST_CASE("alert payload  set cancel accessor 1  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, set_cancel, min_version, max_version, set_sub_version,
                                    priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(set_cancel == instance.set_cancel());
}

TEST_CASE("alert payload  set cancel accessor 2  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    const message::alert_payload instance(version, relay_until, expiration, id,
                                          cancel, set_cancel, min_version, max_version, set_sub_version,
                                          priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(set_cancel == instance.set_cancel());
}

TEST_CASE("alert payload  set cancel setter 1  roundtrip  success", "[alert payload]") {
    const std::vector<uint32_t> value = {68215u, 34542u, 4756u};
    message::alert_payload instance;
    REQUIRE(instance.set_cancel() != value);
    instance.set_set_cancel(value);
    REQUIRE(value == instance.set_cancel());
}

TEST_CASE("alert payload  set cancel setter 2  roundtrip  success", "[alert payload]") {
    const std::vector<uint32_t> value = {68215u, 34542u, 4756u};
    auto dup_value = value;
    message::alert_payload instance;
    REQUIRE(instance.set_cancel() != value);
    instance.set_set_cancel(std::move(dup_value));
    REQUIRE(value == instance.set_cancel());
}

TEST_CASE("alert payload  min version  roundtrip  success", "[alert payload]") {
    uint32_t value = 68215u;
    message::alert_payload instance;
    REQUIRE(instance.min_version() != value);
    instance.set_min_version(value);
    REQUIRE(value == instance.min_version());
}

TEST_CASE("alert payload  max version  roundtrip  success", "[alert payload]") {
    uint32_t value = 68215u;
    message::alert_payload instance;
    REQUIRE(instance.max_version() != value);
    instance.set_max_version(value);
    REQUIRE(value == instance.max_version());
}

TEST_CASE("alert payload  set sub version accessor 1  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, set_cancel, min_version, max_version, set_sub_version,
                                    priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(set_sub_version == instance.set_sub_version());
}

TEST_CASE("alert payload  set sub version accessor 2  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    const message::alert_payload instance(version, relay_until, expiration, id,
                                          cancel, set_cancel, min_version, max_version, set_sub_version,
                                          priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(set_sub_version == instance.set_sub_version());
}

TEST_CASE("alert payload  set sub version setter 1  roundtrip  success", "[alert payload]") {
    const std::vector<std::string> value = {"asdfa", "sgfdf", "Tryertsd"};
    message::alert_payload instance;
    REQUIRE(instance.set_sub_version() != value);
    instance.set_set_sub_version(value);
    REQUIRE(value == instance.set_sub_version());
}

TEST_CASE("alert payload  set sub version setter 2  roundtrip  success", "[alert payload]") {
    const std::vector<std::string> value = {"asdfa", "sgfdf", "Tryertsd"};
    auto dup_value = value;
    message::alert_payload instance;
    REQUIRE(instance.set_sub_version() != value);
    instance.set_set_sub_version(std::move(dup_value));
    REQUIRE(value == instance.set_sub_version());
}

TEST_CASE("alert payload  priority  roundtrip  success", "[alert payload]") {
    uint32_t value = 68215u;
    message::alert_payload instance;
    REQUIRE(instance.priority() != value);
    instance.set_priority(value);
    REQUIRE(value == instance.priority());
}

TEST_CASE("alert payload  comment accessor 1  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, set_cancel, min_version, max_version, set_sub_version,
                                    priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(comment == instance.comment());
}

TEST_CASE("alert payload  comment accessor 2  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    const message::alert_payload instance(version, relay_until, expiration, id,
                                          cancel, set_cancel, min_version, max_version, set_sub_version,
                                          priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(comment == instance.comment());
}

TEST_CASE("alert payload  comment setter 1  roundtrip  success", "[alert payload]") {
    std::string const value = "asdfa";
    message::alert_payload instance;
    REQUIRE(instance.comment() != value);
    instance.set_comment(value);
    REQUIRE(value == instance.comment());
}

TEST_CASE("alert payload  comment setter 2  roundtrip  success", "[alert payload]") {
    std::string const value = "Tryertsd";
    auto dup_value = value;
    message::alert_payload instance;
    REQUIRE(instance.comment() != value);
    instance.set_comment(std::move(dup_value));
    REQUIRE(value == instance.comment());
}

TEST_CASE("alert payload  status bar accessor 1  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, set_cancel, min_version, max_version, set_sub_version,
                                    priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(status_bar == instance.status_bar());
}

TEST_CASE("alert payload  status bar accessor 2  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    const message::alert_payload instance(version, relay_until, expiration, id,
                                          cancel, set_cancel, min_version, max_version, set_sub_version,
                                          priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(status_bar == instance.status_bar());
}

TEST_CASE("alert payload  status bar setter 1  roundtrip  success", "[alert payload]") {
    std::string const value = "asdfa";
    message::alert_payload instance;
    REQUIRE(instance.status_bar() != value);
    instance.set_status_bar(value);
    REQUIRE(value == instance.status_bar());
}

TEST_CASE("alert payload  status bar setter 2  roundtrip  success", "[alert payload]") {
    std::string const value = "Tryertsd";
    auto dup_value = value;
    message::alert_payload instance;
    REQUIRE(instance.status_bar() != value);
    instance.set_status_bar(std::move(dup_value));
    REQUIRE(value == instance.status_bar());
}

TEST_CASE("alert payload  reserved accessor 1  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload instance(version, relay_until, expiration, id,
                                    cancel, set_cancel, min_version, max_version, set_sub_version,
                                    priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload  reserved accessor 2  always  returns initialized", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    const message::alert_payload instance(version, relay_until, expiration, id,
                                          cancel, set_cancel, min_version, max_version, set_sub_version,
                                          priority, comment, status_bar, reserved);

    REQUIRE(instance.is_valid());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload  reserved setter 1  roundtrip  success", "[alert payload]") {
    std::string const value = "asdfa";
    message::alert_payload instance;
    REQUIRE(instance.reserved() != value);
    instance.set_reserved(value);
    REQUIRE(value == instance.reserved());
}

TEST_CASE("alert payload  reserved setter 2  roundtrip  success", "[alert payload]") {
    std::string const value = "Tryertsd";
    auto dup_value = value;
    message::alert_payload instance;
    REQUIRE(instance.reserved() != value);
    instance.set_reserved(std::move(dup_value));
    REQUIRE(value == instance.reserved());
}

TEST_CASE("alert payload  operator assign equals  always  matches equivalent", "[alert payload]") {
    uint32_t const version = 3452u;
    uint64_t const relay_until = 64556u;
    uint64_t const expiration = 78545u;
    uint32_t const id = 43547u;
    uint32_t const cancel = 546562345u;
    const std::vector<uint32_t> set_cancel = {2345u, 346754u, 234u, 4356u};
    uint32_t const min_version = 4644u;
    uint32_t const max_version = 89876u;
    const std::vector<std::string> set_sub_version = {"foo", "bar", "baz"};
    uint32_t const priority = 34323u;
    std::string const comment = "asfgsddsa";
    std::string const status_bar = "fgjdfhjg";
    std::string const reserved = "utyurtevc";

    message::alert_payload value(version, relay_until, expiration, id,
                                 cancel, set_cancel, min_version, max_version, set_sub_version,
                                 priority, comment, status_bar, reserved);

    REQUIRE(value.is_valid());

    message::alert_payload instance;
    REQUIRE( ! instance.is_valid());

    instance = std::move(value);
    REQUIRE(instance.is_valid());
    REQUIRE(version == instance.version());
    REQUIRE(relay_until == instance.relay_until());
    REQUIRE(expiration == instance.expiration());
    REQUIRE(id == instance.id());
    REQUIRE(cancel == instance.cancel());
    REQUIRE(set_cancel == instance.set_cancel());
    REQUIRE(min_version == instance.min_version());
    REQUIRE(max_version == instance.max_version());
    REQUIRE(set_sub_version == instance.set_sub_version());
    REQUIRE(priority == instance.priority());
    REQUIRE(comment == instance.comment());
    REQUIRE(status_bar == instance.status_bar());
    REQUIRE(reserved == instance.reserved());
}

TEST_CASE("alert payload  operator boolean equals  duplicates  returns true", "[alert payload]") {
    const message::alert_payload expected(3452u, 64556u, 78545u, 43547u,
                                          546562345u, {2345u, 346754u, 234u, 4356u}, 4644u, 89876u,
                                          {"foo", "bar", "baz"}, 34323u, "asfgsddsa", "fgjdfhjg", "utyurtevc");

    message::alert_payload instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("alert payload  operator boolean equals  differs  returns false", "[alert payload]") {
    const message::alert_payload expected(3452u, 64556u, 78545u, 43547u,
                                          546562345u, {2345u, 346754u, 234u, 4356u}, 4644u, 89876u,
                                          {"foo", "bar", "baz"}, 34323u, "asfgsddsa", "fgjdfhjg", "utyurtevc");

    message::alert_payload instance;
    REQUIRE(instance != expected);
}

TEST_CASE("alert payload  operator boolean not equals  duplicates  returns false", "[alert payload]") {
    const message::alert_payload expected(3452u, 64556u, 78545u, 43547u,
                                          546562345u, {2345u, 346754u, 234u, 4356u}, 4644u, 89876u,
                                          {"foo", "bar", "baz"}, 34323u, "asfgsddsa", "fgjdfhjg", "utyurtevc");

    message::alert_payload instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("alert payload  operator boolean not equals  differs  returns true", "[alert payload]") {
    const message::alert_payload expected(3452u, 64556u, 78545u, 43547u,
                                          546562345u, {2345u, 346754u, 234u, 4356u}, 4644u, 89876u,
                                          {"foo", "bar", "baz"}, 34323u, "asfgsddsa", "fgjdfhjg", "utyurtevc");

    message::alert_payload instance;
    REQUIRE(instance != expected);
}

// End Test Suite
