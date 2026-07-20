// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: filter add tests
TEST_CASE("filter add constructor 2 always equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    auto const instance = message::filter_add::create(data).value();
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add constructor 3 always equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    auto dup = data;
    auto const instance = message::filter_add::create(std::move(dup)).value();
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add constructor 4 always equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    auto const value = message::filter_add::create(data).value();
    message::filter_add instance(value);
    REQUIRE(value == instance);
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add constructor 5 always equals params", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    auto value = message::filter_add::create(data).value();
    message::filter_add instance(std::move(value));
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add from data insufficient bytes failure", "[filter add]") {
    data_chunk raw = {0xab, 0x11};
    message::filter_add instance;

    byte_reader reader(raw);
    auto result = message::filter_add::from_data(reader, message::version::level::maximum);
    REQUIRE( ! result);
}

TEST_CASE("filter add from data insufficient version failure", "[filter add]") {
    auto const expected = message::filter_add::create({
        {0x1F, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
         0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
         0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
         0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}).value();

    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);
    byte_reader reader(data);
    auto const result = message::filter_add::from_data(reader, message::version::level::minimum - 1);
    REQUIRE( ! result);
    REQUIRE(result.error() == error::version_too_low);
}

TEST_CASE("filter add from data valid input success", "[filter add]") {
    auto const expected = message::filter_add::create({
        {0x1F, 0x9a, 0x0d, 0x24, 0x9a, 0xd5, 0x39, 0x89,
         0xbb, 0x85, 0x0a, 0x3d, 0x79, 0x24, 0xed, 0x0f,
         0xc3, 0x0d, 0x6f, 0x55, 0x7d, 0x71, 0x12, 0x1a,
         0x37, 0xc0, 0xb0, 0x32, 0xf0, 0xd6, 0x6e, 0xdf}}).value();

    auto const data = kth::to_data_chunk(expected, message::version::level::maximum);
    byte_reader reader(data);
    auto const result_exp = message::filter_add::from_data(reader, message::version::level::maximum);
    REQUIRE(result_exp);
    auto const result = std::move(*result_exp);

    REQUIRE(expected == result);
    REQUIRE(data.size() == result.serialized_size(message::version::level::maximum));
    REQUIRE(expected.serialized_size(message::version::level::maximum) ==
                        result.serialized_size(message::version::level::maximum));
}

TEST_CASE("filter add data accessor 1 always returns initialized value", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};
    auto const instance = message::filter_add::create(value).value();
    REQUIRE(value == instance.data());
}

TEST_CASE("filter add data accessor 2 always returns initialized value", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};
    auto const instance = message::filter_add::create(value).value();
    REQUIRE(value == instance.data());
}

TEST_CASE("filter add data is set at construction", "[filter add]") {
    data_chunk const value = {0x0f, 0xf0, 0x55, 0xaa};

    message::filter_add const empty;
    REQUIRE(value != empty.data());

    auto const instance = message::filter_add::create(value).value();
    REQUIRE(value == instance.data());

    data_chunk dup = value;
    auto const moved = message::filter_add::create(std::move(dup)).value();
    REQUIRE(value == moved.data());
}

TEST_CASE("filter add create rejects data over the BIP37 cap", "[filter add]") {
    auto const at_cap = message::filter_add::create(data_chunk(max_filter_add, 0x00));
    REQUIRE(at_cap);
    REQUIRE(at_cap->data().size() == max_filter_add);

    auto const over = message::filter_add::create(data_chunk(max_filter_add + 1, 0x00));
    REQUIRE( ! over);
    REQUIRE(over.error() == error::invalid_filter_add);
}

TEST_CASE("filter add operator assign equals always matches equivalent", "[filter add]") {
    data_chunk const data = {0x0f, 0xf0, 0x55, 0xaa};
    auto value = message::filter_add::create(data).value();
    message::filter_add instance;
    instance = std::move(value);
    REQUIRE(data == instance.data());
}

TEST_CASE("filter add operator boolean equals duplicates returns true", "[filter add]") {
    auto const expected = message::filter_add::create({0x0f, 0xf0, 0x55, 0xaa}).value();
    message::filter_add instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter add operator boolean equals differs returns false", "[filter add]") {
    auto const expected = message::filter_add::create({0x0f, 0xf0, 0x55, 0xaa}).value();
    message::filter_add instance;
    REQUIRE(instance != expected);
}

TEST_CASE("filter add operator boolean not equals duplicates returns false", "[filter add]") {
    auto const expected = message::filter_add::create({0x0f, 0xf0, 0x55, 0xaa}).value();
    message::filter_add instance(expected);
    REQUIRE(instance == expected);
}

TEST_CASE("filter add operator boolean not equals differs returns true", "[filter add]") {
    auto const expected = message::filter_add::create({0x0f, 0xf0, 0x55, 0xaa}).value();
    message::filter_add instance;
    REQUIRE(instance != expected);
}

// End Test Suite
