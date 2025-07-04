// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#define FMT_HEADER_ONLY 1
#include <fmt/core.h>

#include <kth/infrastructure.hpp>

#include "number.hpp"
#ifdef ENABLE_DATAGEN
#include "big_number.hpp"
#endif

// Start Test Suite: number tests

using namespace kth;
using namespace kth::infrastructure::machine;

// Helpers
// ----------------------------------------------------------------------------

#define KI_SCRIPT_NUMBER_CHECK_EQ(buffer_num, script_num, value, offset, test) \
    CHECK_MESSAGE( \
    	encode_base16((buffer_num).bytes) == encode_base16((script_num).data()), \
    	"\n\tvalue index : " << value << \
		"\n\tvalue       : " << number_values[value] << \
		"\n\toffset index: " << offset << \
		"\n\toffset      : " << number_offsets[offset] << \
		"\n\ttest        : " << test << \
		"\n\tFAILURE     : [" << encode_base16((buffer_num).bytes) << " != " << \
		encode_base16((script_num).data()) << "]"); \
    CHECK_MESSAGE((buffer_num).number == (script_num).int32(), \
       	"\n\tvalue index : " << value << \
   		"\n\tvalue       : " << number_values[value] << \
   		"\n\toffset index: " << offset << \
    	"\n\toffset      : " << number_offsets[offset] << \
		"\n\ttest        : " << test << \
		"\n\tFAILURE     : [" << (buffer_num).number << " != " << \
		(script_num).int32() << "]")

static
bool is(uint8_t byte) {
    return byte != 0;
}

// check left - right
static
bool subtract_overflow64(int64_t const left, int64_t const right) {
    return
        ((right > 0 && left < std::numeric_limits<int64_t>::min() + right) ||
        (right < 0 && left > std::numeric_limits<int64_t>::max() + right));
}

static
bool add_overflow64(int64_t const left, int64_t const right) {
    return
        ((right > 0 && left > (std::numeric_limits<int64_t>::max() - right)) ||
        (right < 0 && left < (std::numeric_limits<int64_t>::min() - right)));
}

static
bool negate_overflow64(int64_t const number) {
    return number == std::numeric_limits<int64_t>::min();
}

// Operators
// ----------------------------------------------------------------------------

static
void CheckAdd(int64_t const num1, int64_t const num2, size_t value, size_t offset, size_t test) {
	number_buffer const& add = number_adds[value][offset][test];
    auto const scriptnum1 = number::from_int(num1).value();
    auto const scriptnum2 = number::from_int(num2).value();

    if ( ! add_overflow64(num1, num2)) {
        KI_SCRIPT_NUMBER_CHECK_EQ(add, scriptnum1 + scriptnum2, value, offset, test);
        KI_SCRIPT_NUMBER_CHECK_EQ(add, scriptnum1 + num2, value, offset, test);
        KI_SCRIPT_NUMBER_CHECK_EQ(add, scriptnum2 + num1, value, offset, test);
    }
}

static
void CheckNegate(int64_t const num, size_t value, size_t offset, size_t test) {
	number_buffer const& negated = number_negates[value][offset][test];
    auto const scriptnum = number::from_int(num).value();

    if ( ! negate_overflow64(num)) {
        KI_SCRIPT_NUMBER_CHECK_EQ(negated, -scriptnum, value, offset, test);
    }
}

static
void CheckSubtract(int64_t const num1, int64_t const num2, size_t value, size_t offset, size_t test) {
	number_subtract const& subtract = number_subtracts[value][offset][test];
    auto const scriptnum1 = number::from_int(num1).value();
    auto const scriptnum2 = number::from_int(num2).value();

    if ( ! subtract_overflow64(num1, num2)) {
        KI_SCRIPT_NUMBER_CHECK_EQ(subtract.forward, scriptnum1 - scriptnum2, value, offset, test);
        KI_SCRIPT_NUMBER_CHECK_EQ(subtract.forward, scriptnum1 - num2, value, offset, test);
    }

    if ( ! subtract_overflow64(num2, num1)) {
        KI_SCRIPT_NUMBER_CHECK_EQ(subtract.reverse, scriptnum2 - scriptnum1, value, offset, test);
        KI_SCRIPT_NUMBER_CHECK_EQ(subtract.reverse, scriptnum2 - num1, value, offset, test);
    }
}

static
void CheckCompare(int64_t const num1, int64_t const num2, size_t value, size_t offset, size_t test) {
    number_compare const& compare = number_compares[value][offset][test];
    auto const scriptnum1 = number::from_int(num1).value();
    auto const scriptnum2 = number::from_int(num2).value();

    CHECK(scriptnum1 == scriptnum1);
    CHECK(scriptnum1 >= scriptnum1);
    CHECK(scriptnum1 <= scriptnum1);
    CHECK( ! (scriptnum1 != scriptnum1));
    CHECK( ! (scriptnum1 < scriptnum1));
    CHECK( ! (scriptnum1 > scriptnum1));

    CHECK(scriptnum1 == num1);
    CHECK(scriptnum1 >= num1);
    CHECK(scriptnum1 <= num1);
    CHECK( ! (scriptnum1 != num1));
    CHECK( ! (scriptnum1 < num1));
    CHECK( ! (scriptnum1 > num1));

    CHECK(is(compare.eq) == (scriptnum1 == scriptnum2));
    CHECK(is(compare.ge) == (scriptnum1 >= scriptnum2));
    CHECK(is(compare.le) == (scriptnum1 <= scriptnum2));
    CHECK(is(compare.ne) == (scriptnum1 != scriptnum2));
    CHECK(is(compare.lt) == (scriptnum1 < scriptnum2));
    CHECK(is(compare.gt) == (scriptnum1 > scriptnum2));

    CHECK(is(compare.eq) == (scriptnum1 == num2));
    CHECK(is(compare.ge) == (scriptnum1 >= num2));
    CHECK(is(compare.le) == (scriptnum1 <= num2));
    CHECK(is(compare.ne) == (scriptnum1 != num2));
    CHECK(is(compare.lt) == (scriptnum1 < num2));
    CHECK(is(compare.gt) == (scriptnum1 > num2));
}

#ifndef ENABLE_DATAGEN

// Test
// ----------------------------------------------------------------------------

static
void RunOperators(int64_t const num1, int64_t num2, size_t value, size_t offset, size_t test) {
    //// Diagnostics
    //std::stringstream message;
    //std::cout << boost::format(
    //    ">>> RunOperators: {} : {} : {} : {} : {}\n")
    //    % num1 % num2 % value % offset % test;
    //BOOST_MESSAGE(message.str());

	CheckAdd(num1, num2, value, offset, test);
	CheckNegate(num1, value, offset, test);
	CheckSubtract(num1, num2, value, offset, test);
	CheckCompare(num1, num2, value, offset, test);
}

TEST_CASE("check operators", "[number tests]") {
    for (size_t i = 0; i < number_values_count; ++i) {
        for (size_t j = 0; j < number_offsets_count; ++j) {
            auto a = number_values[i];
            auto b = number_offsets[j];

            RunOperators(a, +a,         i, j, 0);
            RunOperators(a, -a,         i, j, 1);
            RunOperators(a, +b,         i, j, 2);
            RunOperators(a, -b,         i, j, 3);
            RunOperators(a + b, +b,     i, j, 4);
            RunOperators(a + b, -b,     i, j, 5);
            RunOperators(a - b, +b,     i, j, 6);
            RunOperators(a - b, -b,     i, j, 7);
            RunOperators(a + b, +a + b, i, j, 8);
            RunOperators(a + b, +a - b, i, j, 9);
            RunOperators(a - b, +a + b, i, j, 10);
            RunOperators(a - b, +a - b, i, j, 11);
        }
    }
}

#else

// big_number value generators
// ----------------------------------------------------------------------------

static
number_buffer MakeAdd(int64_t const num1, int64_t const num2) {
    if (add_overflow64(num1, num2))
        return number_buffer();

    big_number bignum1;
    bignum1.set_int64(num1);
    big_number bignum2;
    bignum2.set_int64(num2);

    auto sum = bignum1 + bignum2;
    number_buffer const add {
        sum.int32(),
        sum.data()
    };

    return add;
}

static
number_buffer MakeNegate(int64_t const num) {
    if (negate_overflow64(num)) {
        return number_buffer();
    }

    big_number bignum;
    bignum.set_int64(num);

    auto negative = -bignum;
    number_buffer const negated {
        negative.int32(),
        negative.data()
    };

    return negated;
}

static
number_subtract MakeSubtract(int64_t const num1, int64_t const num2) {
    big_number bignum1;
    bignum1.set_int64(num1);
    big_number bignum2;
    bignum2.set_int64(num2);

    big_number forward;
    if ( ! subtract_overflow64(num1, num2)) {
        forward = bignum1 - bignum2;
    }

    big_number reverse;
    if ( ! subtract_overflow64(num2, num1)) {
        reverse = bignum2 - bignum1;
    }

    number_subtract const subtract {
        { forward.int32(), forward.data() },
        { reverse.int32(), reverse.data() }
    };

    return subtract;
}

static number_compare MakeCompare(int64_t const num1, int64_t const num2) {
    big_number bignum1;
    bignum1.set_int64(num1);
    big_number bignum2;
    bignum2.set_int64(num2);

    number_compare compare {
        bignum1 == bignum2,
        bignum1 != bignum2,
        bignum1 < bignum2,
        bignum1 > bignum2,
        bignum1 <= bignum2,
        bignum1 >= bignum2
    };

    return compare;
}

// Formatter Helpers
// ----------------------------------------------------------------------------

static
void write_bytes(kth::data_chunk chunk, std::ostream& out) {
    for (auto const& byte : chunk)
        out << fmt::format(" 0x%02x, ", static_cast<uint16_t>(byte));
}

static
void write_buffer(number_buffer buffer, std::ostream& out) {
    out << fmt::format("{ {}, {", buffer.number);
    write_bytes(buffer.bytes, out);
    out << "} }, ";
}

static
void write_compare(number_compare compare, std::ostream& out) {
    out << fmt::format("{ {}, {}, {}, {}, {}, {} }, ", compare.eq, compare.ne, compare.lt, compare.gt, compare.le, compare.ge);
}

static
void write_subtract(number_subtract subtract, std::ostream& out) {
    out << "{ ";
    write_buffer(subtract.forward, out);
    write_buffer(subtract.reverse, out);
    out << "}, ";
}

static
void write_names(std::string const& name, size_t count, std::ostream& out) {
    out << fmt::format("const [{}][{}][{}][{}]=\n{\n", name, number_values_count, number_offsets_count, count);
}

static
void write(std::string const& text, std::ostream& add_out, std::ostream& neg_out, std::ostream& sub_out, std::ostream& cmp_out) {
    add_out << text;
    neg_out << text;
    sub_out << text;
    cmp_out << text;
}

static
void replace(std::string& buffer, std::string const& find, std::string const& replacement) {
    size_t pos = 0;
    while ((pos = buffer.find(find, pos)) != std::string::npos) {
        buffer.replace(pos, find.length(), replacement);
        pos += replacement.length();
    }
}

// Maker
// ----------------------------------------------------------------------------

static
void MakeOperators(int64_t const num1, int64_t const num2,
    std::ostream& add_out, std::ostream& neg_out, std::ostream& sub_out,
    std::ostream& cmp_out) {
    write("\n              ", add_out, neg_out, sub_out, cmp_out);

    auto add = MakeAdd(num1, num2);
    CheckAdd(num1, num2, add);
    write_buffer(add, add_out);

    auto negate = MakeNegate(num1);
    CheckNegate(num1, negate);
    write_buffer(negate, neg_out);

    auto subtract = MakeSubtract(num1, num2);
    CheckSubtract(num1, num2, subtract);
    write_subtract(subtract, sub_out);

    auto compare = MakeCompare(num1, num2);
    CheckCompare(num1, num2, compare);
    write_compare(compare, cmp_out);
}

TEST_CASE("make operator expectations", "[number tests]") {
    std::stringstream add_out;
    std::stringstream neg_out;
    std::stringstream sub_out;
    std::stringstream cmp_out;

    write_names("number_buffer number_adds", 12, add_out);
    write_names("number_buffer number_negates", 12, neg_out);
    write_names("number_subtract number_subtracts", 12, sub_out);
    write_names("number_compare number_compares", 12, cmp_out);

    for (size_t i = 0; i < number_values_count; ++i) {
        write("    {\n", add_out, neg_out, sub_out, cmp_out);

        for (size_t j = 0; j < number_offsets_count; ++j)
        {
            write("        {", add_out, neg_out, sub_out, cmp_out);

            auto a = number_values[i];
            auto b = number_offsets[j];

            MakeOperators(a, +a, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a, -a, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a, +b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a, -b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a + b, +b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a + b, -b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a - b, +b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a - b, -b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a + b, +a + b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a + b, +a - b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a - b, +a + b, add_out, neg_out, sub_out, cmp_out);
            MakeOperators(a - b, +a - b, add_out, neg_out, sub_out, cmp_out);

            write("\n        },\n", add_out, neg_out, sub_out, cmp_out);
        }

        write("    },\n", add_out, neg_out, sub_out, cmp_out);
    }

    write("};\n\n", add_out, neg_out, sub_out, cmp_out);

    std::stringstream dump;
    dump << add_out.str();
    dump << neg_out.str();
    dump << sub_out.str();
    dump << cmp_out.str();

    auto source = dump.str();
    replace(source, "-2147483648", "(-2147483647 - 1)");
    replace(source, "-9223372036854775808", "(-9223372036854775807 - 1)");
}
#endif

// End Test Suite
