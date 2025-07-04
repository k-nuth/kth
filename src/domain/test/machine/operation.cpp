// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::machine;

data_chunk valid_raw_operation = to_chunk(base16_literal("0900ff11ee22bb33aa44"));

// Start Test Suite: operation tests

TEST_CASE("operation  constructor 1  always  returns default initialized", "[operation]") {
    operation instance;

    REQUIRE( ! instance.is_valid());
    REQUIRE(instance.data().empty());
    // REQUIRE(instance.code() == opcode::disabled_xor);
    REQUIRE(instance.code() == opcode::invalidopcode);
}

TEST_CASE("operation  constructor 2  valid input  returns input initialized", "[operation]") {
    auto const data = to_chunk(base16_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));
    auto dup_data = data;
    operation instance(std::move(dup_data));

    REQUIRE(instance.is_valid());
    REQUIRE(instance.code() == opcode::push_size_32);
    REQUIRE(instance.data() == data);
}

TEST_CASE("operation  constructor 3  valid input  returns input initialized", "[operation]") {
    auto const data = to_chunk(base16_literal("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"));
    operation instance(data);

    REQUIRE(instance.is_valid());
    REQUIRE(instance.code() == opcode::push_size_32);
    REQUIRE(instance.data() == data);
}

TEST_CASE("operation  constructor 4  valid input  returns input initialized", "[operation]") {
    operation const expected(to_chunk(base16_literal("23156214")));
    operation instance(expected);

    REQUIRE(instance.is_valid());
    REQUIRE(expected == instance);
}

TEST_CASE("operation  constructor 5  valid input  returns input initialized", "[operation]") {
    operation expected(to_chunk(base16_literal("23156214")));
    operation instance(std::move(expected));

    REQUIRE(instance.is_valid());
}

TEST_CASE("operation from data insufficient bytes  failure", "[operation]") {
    data_chunk const data;
    byte_reader reader(data);
    auto result = operation::from_data(reader);
    REQUIRE( ! result);
}

TEST_CASE("operation from data roundtrip push size 0  success", "[operation]") {
    auto const data0 = to_chunk(base16_literal(""));
    auto const raw_operation = to_chunk(base16_literal("00"));
    operation instance;

    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    REQUIRE(instance.code() == opcode::push_size_0);
    REQUIRE(instance.data() == data0);
}

TEST_CASE("operation from data roundtrip push size 75  success", "[operation]") {
    auto const data75 = data_chunk(75, '.');
    auto const raw_operation = build_chunk({base16_literal("4b"), data75});
    operation instance;

    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    REQUIRE(instance.code() == opcode::push_size_75);
    REQUIRE(instance.data() == data75);
}

TEST_CASE("operation from data roundtrip push negative 1  success", "[operation]") {
    static auto const op_79 = static_cast<uint8_t>(opcode::push_negative_1);
    auto const data1 = data_chunk{op_79};
    auto const raw_operation = data1;
    operation instance;

    // This is read as an encoded operation, not as data.
    // Constructors read (unencoded) data and can select minimal encoding.
    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    // The code is the data for numeric push codes.
    REQUIRE(instance.code() == opcode::push_negative_1);
    REQUIRE(instance.data() == data_chunk{});
}

TEST_CASE("operation from data roundtrip push positive 1  success", "[operation]") {
    static auto const op_81 = static_cast<uint8_t>(opcode::push_positive_1);
    auto const data1 = data_chunk{op_81};
    auto const raw_operation = data1;
    operation instance;

    // This is read as an encoded operation, not as data.
    // Constructors read (unencoded) data and can select minimal encoding.
    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    // The code is the data for numeric push codes.
    REQUIRE(instance.code() == opcode::push_positive_1);
    REQUIRE(instance.data() == data_chunk{});
}

TEST_CASE("operation from data roundtrip push positive 16  success", "[operation]") {
    static auto const op_96 = static_cast<uint8_t>(opcode::push_positive_16);
    auto const data1 = data_chunk{op_96};
    auto const raw_operation = data1;

    // This is read as an encoded operation, not as data.
    // Constructors read (unencoded) data and can select minimal encoding.
    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    auto const instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    // The code is the data for numeric push codes.
    REQUIRE(instance.code() == opcode::push_positive_16);
    REQUIRE(instance.data() == data_chunk{});
}

TEST_CASE("operation from data roundtrip push one size  success", "[operation]") {
    auto const data255 = data_chunk(255, '.');
    auto const raw_operation = build_chunk({base16_literal("4c"
                                                           "ff"),
                                            data255});
    operation instance;

    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    REQUIRE(instance.code() == opcode::push_one_size);
    REQUIRE(instance.data() == data255);
}

TEST_CASE("operation from data roundtrip push two size  success", "[operation]") {
    auto const data520 = data_chunk(520, '.');
    auto const raw_operation = build_chunk({base16_literal("4d"
                                                           "0802"),
                                            data520});
    operation instance;

    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    // operation duplicate;
    // REQUIRE(entity_from_data(duplicate,instance.to_data()));
    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    REQUIRE(instance.code() == opcode::push_two_size);
    REQUIRE(instance.data() == data520);
}

TEST_CASE("operation from data roundtrip push four size  success", "[operation]") {
    auto const data520 = data_chunk(520, '.');
    auto const raw_operation = build_chunk({base16_literal("4e"
                                                           "08020000"),
                                            data520});
    operation instance;

    byte_reader reader(raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    instance = std::move(*result);
    REQUIRE(instance.is_valid());
    REQUIRE(raw_operation == instance.to_data());

    auto const instance_data = instance.to_data();
    byte_reader reader2(instance_data);
    auto result2 = operation::from_data(reader2);
    REQUIRE(result2);
    operation duplicate = std::move(*result2);
    REQUIRE(instance == duplicate);

    REQUIRE(instance.code() == opcode::push_four_size);
    REQUIRE(instance.data() == data520);
}

TEST_CASE("operation from data roundtrip  success", "[operation]") {
    byte_reader reader(valid_raw_operation);
    auto result_exp = machine::operation::from_data(reader);
    REQUIRE(result_exp);
    auto const operation = std::move(*result_exp);

    REQUIRE(operation.is_valid());
    data_chunk output = operation.to_data();
    REQUIRE(output == valid_raw_operation);
}

TEST_CASE("operation  operator assign equals 1  always  matches equivalent", "[operation]") {
    byte_reader reader(valid_raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    operation instance;
    operation value;
    reader.reset();
    result = operation::from_data(reader);
    REQUIRE(result);
    value = std::move(*result);
    instance = std::move(value);
    REQUIRE(instance == expected);
}

TEST_CASE("operation  operator assign equals 2  always  matches equivalent", "[operation]") {
    byte_reader reader(valid_raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    auto const expected = std::move(*result);
    operation instance;
    instance = expected;
    REQUIRE(instance == expected);
}

TEST_CASE("operation  operator boolean equals  duplicates  returns true", "[operation]") {
    operation alpha;
    operation beta;
    byte_reader reader(valid_raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = operation::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("operation  operator boolean equals  differs  returns false", "[operation]") {
    operation alpha;
    operation beta;
    byte_reader reader(valid_raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

TEST_CASE("operation  operator boolean not equals  duplicates  returns false", "[operation]") {
    operation alpha;
    operation beta;
    byte_reader reader(valid_raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    reader.reset();
    result = operation::from_data(reader);
    REQUIRE(result);
    beta = std::move(*result);
    REQUIRE(alpha == beta);
}

TEST_CASE("operation  operator boolean not equals  differs  returns true", "[operation]") {
    operation alpha;
    operation beta;
    byte_reader reader(valid_raw_operation);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    alpha = std::move(*result);
    REQUIRE(alpha != beta);
}

// to_string

TEST_CASE("operation  to string  push size 0  zero", "[operation]") {
    operation value(opcode::push_size_0);
    REQUIRE(value.to_string(0) == "zero");
}

TEST_CASE("operation  to string  push size 75  push 75", "[operation]") {
    // Empty data allows the push code to serialize as an op code.
    operation value(opcode::push_size_75);
    REQUIRE(value.to_string(0) == "push_75");
}

TEST_CASE("operation  to string  push positive 7  7", "[operation]") {
    operation value(opcode::push_positive_7);
    REQUIRE(value.to_string(0) == "7");
}

TEST_CASE("operation  to string minimal  0x07  7", "[operation]") {
    operation value({0x07}, true);
    REQUIRE(value.to_string(0) == "7");
}

TEST_CASE("operation  to string nominal  0x07  0x07", "[operation]") {
    operation value({0x07}, false);
    REQUIRE(value.to_string(0) == "[07]");
}

TEST_CASE("operation  to string  0x42  0x42", "[operation]") {
    operation value({0x42}, true);
    REQUIRE(value.to_string(0) == "[42]");
}

TEST_CASE("operation  to string  0x112233  0x112233", "[operation]") {
    operation value({{0x11, 0x22, 0x33}}, true);
    REQUIRE(value.to_string(0) == "[112233]");
}

TEST_CASE("operation  to string  push size 3  0x112233  0x112233", "[operation]") {
    static data_chunk const encoded{{0x03, 0x11, 0x22, 0x33}};

    byte_reader reader(encoded);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    operation value = std::move(*result);
    REQUIRE(value.to_string(0) == "[112233]");
}

TEST_CASE("operation  to string  push one size 0x112233  1 0x112233", "[operation]") {
    static data_chunk const encoded{{0x4c, 0x03, 0x11, 0x22, 0x33}};

    byte_reader reader(encoded);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    operation value = std::move(*result);
    REQUIRE(value.to_string(0) == "[1.112233]");
}

TEST_CASE("operation  to string  push two size 0x112233  2 0x112233", "[operation]") {
    static data_chunk const encoded{{0x4d, 0x03, 0x00, 0x11, 0x22, 0x33}};

    byte_reader reader(encoded);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    operation value = std::move(*result);
    REQUIRE(value.to_string(0) == "[2.112233]");
}

TEST_CASE("operation  to string  push four size 0x112233  4 0x112233", "[operation]") {
    static data_chunk const encoded{{0x4e, 0x03, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33}};

    byte_reader reader(encoded);
    auto result = operation::from_data(reader);
    REQUIRE(result);
    operation value = std::move(*result);
    REQUIRE(value.to_string(0) == "[4.112233]");
}

TEST_CASE("operation  to string  nop2 no rules  nop2", "[operation]") {
    operation value(opcode::nop2);
    REQUIRE(value.to_string(machine::rule_fork::no_rules) == "nop2");
}

TEST_CASE("operation  to string  nop2 bip65 rule  checklocktimeverify", "[operation]") {
    operation value(opcode::nop2);
    REQUIRE(value.to_string(machine::rule_fork::bip65_rule) == "checklocktimeverify");
}

TEST_CASE("operation  to string  nop3 no rules  nop3", "[operation]") {
    operation value(opcode::nop3);
    REQUIRE(value.to_string(machine::rule_fork::no_rules) == "nop3");
}

TEST_CASE("operation  to string  nop3 bip112 rule  checksequenceverify", "[operation]") {
    operation value(opcode::nop3);
    REQUIRE(value.to_string(machine::rule_fork::bip112_rule) == "checksequenceverify");
}

// from_string

TEST_CASE("operation  from string  negative 1  push negative 1 empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("-1"));
    REQUIRE(value.code() == opcode::push_negative_1);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  0  push size 0 empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("0"));
    REQUIRE(value.code() == opcode::push_size_0);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  1  push positive 1 empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("1"));
    REQUIRE(value.code() == opcode::push_positive_1);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  16  push positive 16 empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("16"));
    REQUIRE(value.code() == opcode::push_positive_16);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  17  push size 1 expected", "[operation]") {
    static data_chunk const expected{0x11};
    operation value;
    REQUIRE(value.from_string("17"));
    REQUIRE(value.code() == opcode::push_size_1);
    bool xxx = value.data() == expected;
    REQUIRE(value.data() == expected);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  negative 2  push size 1 expected", "[operation]") {
    static data_chunk const expected{0x82};
    operation value;
    REQUIRE(value.from_string("-2"));
    REQUIRE(value.code() == opcode::push_size_1);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  9223372036854775807  push size 8 expected", "[operation]") {
    static data_chunk const expected{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}};
    operation value;
    REQUIRE(value.from_string("9223372036854775807"));
    REQUIRE(value.code() == opcode::push_size_8);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  negative 9223372036854775807  push size 8 expected", "[operation]") {
    static data_chunk const expected{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    operation value;
    REQUIRE(value.from_string("-9223372036854775807"));
    REQUIRE(value.code() == opcode::push_size_8);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  string empty  push size 0 empty", "[operation]") {
    static data_chunk const expected{0x61};
    operation value;
    REQUIRE(value.from_string("''"));
    REQUIRE(value.code() == opcode::push_size_0);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  string a  push size 1 expected byte", "[operation]") {
    static data_chunk const expected{0x61};
    operation value;
    REQUIRE(value.from_string("'a'"));
    REQUIRE(value.code() == opcode::push_size_1);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  string abc  push size 3 expected byte order", "[operation]") {
    static data_chunk const expected{{0x61, 0x62, 0x63}};
    operation value;
    REQUIRE(value.from_string("'abc'"));
    REQUIRE(value.code() == opcode::push_size_3);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  negative 1 character  push size 1 nominal encoding", "[operation]") {
    static data_chunk const expected{0x4f};
    operation value;
    REQUIRE(value.from_string("'O'"));
    REQUIRE(value.code() == opcode::push_size_1);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  push 0  push size 0", "[operation]") {
    operation value;
    REQUIRE(value.from_string("push_0"));
    REQUIRE(value.code() == opcode::push_size_0);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  push 1  false", "[operation]") {
    operation value;
    REQUIRE( ! value.from_string("push_1"));
}

TEST_CASE("operation  from string  push 75  false", "[operation]") {
    operation value;
    REQUIRE( ! value.from_string("push_75"));
}

TEST_CASE("operation  from string  push one  push one size empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("push_one"));
    REQUIRE(value.code() == opcode::push_one_size);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  push two  push two size empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("push_two"));
    REQUIRE(value.code() == opcode::push_two_size);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  push four  push four size empty", "[operation]") {
    operation value;
    REQUIRE(value.from_string("push_four"));
    REQUIRE(value.code() == opcode::push_four_size);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  7  push positive 7", "[operation]") {
    operation value;
    REQUIRE(value.from_string("7"));
    REQUIRE(value.code() == opcode::push_positive_7);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  0x07  push size 1", "[operation]") {
    static data_chunk const expected{0x07};
    operation value;
    REQUIRE(value.from_string("[07]"));
    REQUIRE(value.code() == opcode::push_size_1);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  0x42  push size 1", "[operation]") {
    static data_chunk const expected{0x42};
    operation value;
    REQUIRE(value.from_string("[42]"));
    REQUIRE(value.code() == opcode::push_size_1);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  0x112233  push size 3", "[operation]") {
    static data_chunk const expected{{0x11, 0x22, 0x33}};
    operation value;
    REQUIRE(value.from_string("[112233]"));
    REQUIRE(value.code() == opcode::push_size_3);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  0 0x112233  push size 3", "[operation]") {
    static data_chunk const expected{{0x11, 0x22, 0x33}};
    operation value;
    REQUIRE(value.from_string("[0.112233]"));
    REQUIRE(value.code() == opcode::push_size_3);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  1 0x112233  push one size", "[operation]") {
    static data_chunk const expected{{0x11, 0x22, 0x33}};
    operation value;
    REQUIRE(value.from_string("[1.112233]"));
    REQUIRE(value.code() == opcode::push_one_size);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  2 0x112233  push two size", "[operation]") {
    static data_chunk const expected{{0x11, 0x22, 0x33}};
    operation value;
    REQUIRE(value.from_string("[2.112233]"));
    REQUIRE(value.code() == opcode::push_two_size);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  4 0x112233  push four size", "[operation]") {
    static data_chunk const expected{{0x11, 0x22, 0x33}};
    operation value;
    REQUIRE(value.from_string("[4.112233]"));
    REQUIRE(value.code() == opcode::push_four_size);
    REQUIRE(value.data() == expected);
}

TEST_CASE("operation  from string  5 0x112233  false", "[operation]") {
    operation value;
    REQUIRE( ! value.from_string("[5.112233]"));
}

TEST_CASE("operation  from string  empty 0x112233  false", "[operation]") {
    operation value;
    REQUIRE( ! value.from_string("[.112233]"));
}

TEST_CASE("operation  from string  nop2  nop2 checklocktimeverify", "[operation]") {
    operation value;
    REQUIRE(value.from_string("nop2"));
    REQUIRE(value.code() == opcode::nop2);
    REQUIRE(value.code() == opcode::checklocktimeverify);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  checklocktimeverify  nop2 checklocktimeverify", "[operation]") {
    operation value;
    REQUIRE(value.from_string("checklocktimeverify"));
    REQUIRE(value.code() == opcode::nop2);
    REQUIRE(value.code() == opcode::checklocktimeverify);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  nop3  nop3 checksequenceverify", "[operation]") {
    operation value;
    REQUIRE(value.from_string("nop3"));
    REQUIRE(value.code() == opcode::nop3);
    REQUIRE(value.code() == opcode::checksequenceverify);
    REQUIRE(value.data().empty());
}

TEST_CASE("operation  from string  checklocktimeverify  nop3 checksequenceverify", "[operation]") {
    operation value;
    REQUIRE(value.from_string("checksequenceverify"));
    REQUIRE(value.code() == opcode::nop3);
    REQUIRE(value.code() == opcode::checksequenceverify);
    REQUIRE(value.data().empty());
}

// End Test Suite
