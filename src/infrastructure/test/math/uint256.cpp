// // Copyright (c) 2016-2025 Knuth Project developers.
// // Distributed under the MIT software license, see the accompanying
// // file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <boost/test/unit_test.hpp>

// #include <sstream>
// #include <string>
// #include <kth/infrastructure.hpp>

// using namespace kth;

// // Start Test Suite: uint256 tests

// #define MAX_HASH \
// "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
// static auto const max_hash = hash_literal(MAX_HASH);

// #define NEGATIVE1_HASH \
// "8000000000000000000000000000000000000000000000000000000000000000"
// static auto const negative_zero_hash = hash_literal(NEGATIVE1_HASH);

// #define MOST_HASH \
// "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
// static auto const most_hash = hash_literal(MOST_HASH);

// #define ODD_HASH \
// "8437390223499ab234bf128e8cd092343485898923aaaaabbcbcc4874353fff4"
// static auto const odd_hash = hash_literal(ODD_HASH);

// #define HALF_HASH \
// "00000000000000000000000000000000ffffffffffffffffffffffffffffffff"
// static auto const half_hash = hash_literal(HALF_HASH);

// #define QUARTER_HASH \
// "000000000000000000000000000000000000000000000000ffffffffffffffff"
// static auto const quarter_hash = hash_literal(QUARTER_HASH);

// #define UNIT_HASH \
// "0000000000000000000000000000000000000000000000000000000000000001"
// static auto const unit_hash = hash_literal(UNIT_HASH);

// #define ONES_HASH \
// "0000000100000001000000010000000100000001000000010000000100000001"
// static auto const ones_hash = hash_literal(ONES_HASH);

// #define FIVES_HASH \
// "5555555555555555555555555555555555555555555555555555555555555555"
// static auto const fives_hash = hash_literal(FIVES_HASH);

// // constructors
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  constructor default  always  equates to 0", "[uint256 tests]")
// {
//     uint256_t minimum;
//     REQUIRE(minimum > 0 == false);
//     REQUIRE(minimum < 0 == false);
//     REQUIRE(minimum >= 0 == true);
//     REQUIRE(minimum <= 0 == true);
//     REQUIRE(minimum == 0 == true);
//     REQUIRE(minimum != 0 == false);
// }

// TEST_CASE("uint256  constructor move  42  equals 42", "[uint256 tests]")
// {
//     static auto const expected = 42u;
//     static const uint256_t value(uint256_t{ expected });
//     REQUIRE(value == expected);
// }

// TEST_CASE("uint256  constructor copy  odd hash  equals odd hash", "[uint256 tests]")
// {
//     static auto const expected = to_uint256(odd_hash);
//     static const uint256_t value(expected);
//     REQUIRE(value == expected);
// }

// TEST_CASE("uint256  constructor uint32  minimum  equals 0", "[uint256 tests]")
// {
//     static auto const expected = 0u;
//     static const uint256_t value(expected);
//     REQUIRE(value == expected);
// }

// TEST_CASE("uint256  constructor uint32  42  equals 42", "[uint256 tests]")
// {
//     static auto const expected = 42u;
//     static const uint256_t value(expected);
//     REQUIRE(value == expected);
// }

// TEST_CASE("uint256  constructor uint32  maximum  equals maximum", "[uint256 tests]")
// {
//     static auto const expected = max_uint32;
//     static const uint256_t value(expected);
//     REQUIRE(value == expected);
// }

// // bit_length
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  bit length  null hash  returns 0", "[uint256 tests]")
// {
//     static const uint256_t value{ null_hash };
//     REQUIRE(value.bit_length() == 0u);
// }

// TEST_CASE("uint256  bit length  unit hash  returns 1", "[uint256 tests]")
// {
//     static const uint256_t value{ unit_hash };
//     REQUIRE(value.bit_length() == 1u);
// }

// TEST_CASE("uint256  bit length  quarter hash  returns 64", "[uint256 tests]")
// {
//     static const uint256_t value{ quarter_hash };
//     REQUIRE(value.bit_length() == 64u);
// }

// TEST_CASE("uint256  bit length  half hash  returns 128", "[uint256 tests]")
// {
//     static const uint256_t value{ half_hash };
//     REQUIRE(value.bit_length() == 128u);
// }

// TEST_CASE("uint256  bit length  most hash  returns 255", "[uint256 tests]")
// {
//     static const uint256_t value{ most_hash };
//     REQUIRE(value.bit_length() == 255u);
// }

// TEST_CASE("uint256  bit length  negative zero hash  returns 256", "[uint256 tests]")
// {
//     static const uint256_t value{ negative_zero_hash };
//     REQUIRE(value.bit_length() == 256u);
// }

// TEST_CASE("uint256  bit length  max hash  returns 256", "[uint256 tests]")
// {
//     static const uint256_t value{ max_hash };
//     REQUIRE(value.bit_length() == 256u);
// }

// // byte_length
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  byte length  null hash  returns 0", "[uint256 tests]")
// {
//     static const uint256_t value{ null_hash };
//     REQUIRE(value.byte_length() == 0u);
// }

// TEST_CASE("uint256  byte length  unit hash  returns 1", "[uint256 tests]")
// {
//     static const uint256_t value{ unit_hash };
//     REQUIRE(value.byte_length() == 1u);
// }

// TEST_CASE("uint256  byte length  quarter hash  returns 8", "[uint256 tests]")
// {
//     static const uint256_t value{ quarter_hash };
//     REQUIRE(value.byte_length() == 8u);
// }

// TEST_CASE("uint256  byte length  half hash  returns 16", "[uint256 tests]")
// {
//     static const uint256_t value{ half_hash };
//     REQUIRE(value.byte_length() == 16u);
// }

// TEST_CASE("uint256  byte length  most hash  returns 32", "[uint256 tests]")
// {
//     static const uint256_t value{ most_hash };
//     REQUIRE(value.byte_length() == 32u);
// }

// TEST_CASE("uint256  byte length  negative zero hash  returns 32", "[uint256 tests]")
// {
//     static const uint256_t value{ negative_zero_hash };
//     REQUIRE(value.byte_length() == 32u);
// }

// TEST_CASE("uint256  byte length  max hash  returns 32", "[uint256 tests]")
// {
//     static const uint256_t value{ max_hash };
//     REQUIRE(value.byte_length() == 32u);
// }

// // hash
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  hash  default  returns null hash", "[uint256 tests]")
// {
//     static const uint256_t value;
//     REQUIRE(value.hash() == null_hash);
// }

// TEST_CASE("uint256  hash  1  returns unit hash", "[uint256 tests]")
// {
//     static const uint256_t value(1);
//     REQUIRE(value.hash() == unit_hash);
// }

// TEST_CASE("uint256  hash  negative 1  returns negative zero hash", "[uint256 tests]")
// {
//     static const uint256_t value(1);
//     REQUIRE(value.hash() == unit_hash);
// }

// // array operator
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  array  default  expected", "[uint256 tests]")
// {
//     static const uint256_t value;
//     REQUIRE(value[0] == 0x0000000000000000);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x0000000000000000);
// }

// TEST_CASE("uint256  array  42  expected", "[uint256 tests]")
// {
//     static const uint256_t value(42);
//     REQUIRE(value[0] == 0x000000000000002a);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x0000000000000000);
// }

// TEST_CASE("uint256  array  0x87654321  expected", "[uint256 tests]")
// {
//     static const uint256_t value(0x87654321);
//     REQUIRE(value[0] == 0x0000000087654321);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x0000000000000000);
// }

// TEST_CASE("uint256  array  negative 1  expected", "[uint256 tests]")
// {
//     static const uint256_t value(negative_zero_hash);
//     REQUIRE(value[0] == 0x0000000000000000);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x8000000000000000);
// }

// TEST_CASE("uint256  array  odd hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(odd_hash);
//     REQUIRE(value[0] == 0xbcbcc4874353fff4);
//     REQUIRE(value[1] == 0x3485898923aaaaab);
//     REQUIRE(value[2] == 0x34bf128e8cd09234);
//     REQUIRE(value[3] == 0x8437390223499ab2);
// }

// // comparison operators
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  comparison operators  null hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(null_hash);

//     REQUIRE(value > 0 == false);
//     REQUIRE(value < 0 == false);
//     REQUIRE(value >= 0 == true);
//     REQUIRE(value <= 0 == true);
//     REQUIRE(value == 0 == true);
//     REQUIRE(value != 0 == false);

//     REQUIRE(value > 1 == false);
//     REQUIRE(value < 1 == true);
//     REQUIRE(value >= 1 == false);
//     REQUIRE(value <= 1 == true);
//     REQUIRE(value == 1 == false);
//     REQUIRE(value != 1 == true);
// }

// TEST_CASE("uint256  comparison operators  unit hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(unit_hash);

//     REQUIRE(value > 1 == false);
//     REQUIRE(value < 1 == false);
//     REQUIRE(value >= 1 == true);
//     REQUIRE(value <= 1 == true);
//     REQUIRE(value == 1 == true);
//     REQUIRE(value != 1 == false);

//     REQUIRE(value > 0 == true);
//     REQUIRE(value < 0 == false);
//     REQUIRE(value >= 0 == true);
//     REQUIRE(value <= 0 == false);
//     REQUIRE(value == 0 == false);
//     REQUIRE(value != 0 == true);
// }

// TEST_CASE("uint256  comparison operators  negative zero hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(negative_zero_hash);
//     static const uint256_t most(most_hash);
//     static const uint256_t maximum(max_hash);

//     REQUIRE(value > 1 == true);
//     REQUIRE(value < 1 == false);
//     REQUIRE(value >= 1 == true);
//     REQUIRE(value <= 1 == false);
//     REQUIRE(value == 1 == false);
//     REQUIRE(value != 1 == true);

//     BOOST_REQUIRE_GT(value, most);
//     BOOST_REQUIRE_LT(value, maximum);

//     BOOST_REQUIRE_GE(value, most);
//     BOOST_REQUIRE_LE(value, maximum);

//     REQUIRE(value == value);
//     BOOST_REQUIRE_NE(value, most);
//     BOOST_REQUIRE_NE(value, maximum);
// }

// // not
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  not  minimum  maximum", "[uint256 tests]")
// {
//     REQUIRE(~uint256_t() == uint256_t(max_hash));
// }

// TEST_CASE("uint256  not  maximum  minimum", "[uint256 tests]")
// {
//     REQUIRE(~uint256_t(max_hash) == uint256_t());
// }

// TEST_CASE("uint256  not  most hash  negative zero hash", "[uint256 tests]")
// {
//     REQUIRE(~uint256_t(most_hash) == uint256_t(negative_zero_hash));
// }

// TEST_CASE("uint256  not  not odd hash  odd hash", "[uint256 tests]")
// {
//     REQUIRE(~~uint256_t(odd_hash) == uint256_t(odd_hash));
// }

// TEST_CASE("uint256  not  odd hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(odd_hash);
//     static auto const not_value = ~value;
//     REQUIRE(not_value[0] == ~0xbcbcc4874353fff4);
//     REQUIRE(not_value[1] == ~0x3485898923aaaaab);
//     REQUIRE(not_value[2] == ~0x34bf128e8cd09234);
//     REQUIRE(not_value[3] == ~0x8437390223499ab2);
// }

// // two's compliment (negate)
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  twos compliment  null hash  null hash", "[uint256 tests]")
// {
//     REQUIRE(-uint256_t() == uint256_t());
// }

// TEST_CASE("uint256  twos compliment  unit hash  max hash", "[uint256 tests]")
// {
//     REQUIRE(-uint256_t(unit_hash) == uint256_t(max_hash));
// }

// TEST_CASE("uint256  twos compliment  odd hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(odd_hash);
//     static auto const compliment = -value;
//     REQUIRE(compliment[0] == ~0xbcbcc4874353fff4 + 1);
//     REQUIRE(compliment[1] == ~0x3485898923aaaaab);
//     REQUIRE(compliment[2] == ~0x34bf128e8cd09234);
//     REQUIRE(compliment[3] == ~0x8437390223499ab2);
// }

// // shift right
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  shift right  null hash  null hash", "[uint256 tests]")
// {
//     REQUIRE(uint256_t() >> 0 == uint256_t());
//     REQUIRE(uint256_t() >> 1 == uint256_t());
//     REQUIRE(uint256_t() >> max_uint32 == uint256_t());
// }

// TEST_CASE("uint256  shift right  unit hash 0  unit hash", "[uint256 tests]")
// {
//     static const uint256_t value(unit_hash);
//     REQUIRE(value >> 0 == value);
// }

// TEST_CASE("uint256  shift right  unit hash positive  null hash", "[uint256 tests]")
// {
//     static const uint256_t value(unit_hash);
//     REQUIRE(value >> 1 == uint256_t());
//     REQUIRE(value >> max_uint32 == uint256_t());
// }

// TEST_CASE("uint256  shift right  max hash 1  most hash", "[uint256 tests]")
// {
//     static const uint256_t value(max_hash);
//     REQUIRE(value >> 1 == uint256_t(most_hash));
// }

// TEST_CASE("uint256  shift right  odd hash 32  expected", "[uint256 tests]")
// {
//     static const uint256_t value(odd_hash);
//     static auto const shifted = value >> 32;
//     REQUIRE(shifted[0] == 0x23aaaaabbcbcc487);
//     REQUIRE(shifted[1] == 0x8cd0923434858989);
//     REQUIRE(shifted[2] == 0x23499ab234bf128e);
//     REQUIRE(shifted[3] == 0x0000000084373902);
// }

// // add256
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  add256  0 to null hash  null hash", "[uint256 tests]")
// {
//     REQUIRE(uint256_t() + 0 == uint256_t());
// }

// TEST_CASE("uint256  add256  null hash to null hash  null hash", "[uint256 tests]")
// {
//     REQUIRE(uint256_t() + uint256_t() == uint256_t());
// }

// TEST_CASE("uint256  add256  1 to max hash  null hash", "[uint256 tests]")
// {
//     static const uint256_t value(max_hash);
//     static auto const sum = value + 1;
//     REQUIRE(sum == uint256_t());
// }

// TEST_CASE("uint256  add256  ones hash to odd hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(odd_hash);
//     static auto const sum = value + uint256_t(ones_hash);
//     REQUIRE(sum[0] == 0xbcbcc4884353fff5);
//     REQUIRE(sum[1] == 0x3485898a23aaaaac);
//     REQUIRE(sum[2] == 0x34bf128f8cd09235);
//     REQUIRE(sum[3] == 0x8437390323499ab3);
// }

// TEST_CASE("uint256  add256  1 to 0xffffffff  0x0100000000", "[uint256 tests]")
// {
//     static const uint256_t value(0xffffffff);
//     static auto const sum = value + 1;
//     REQUIRE(sum[0] == 0x0000000100000000);
//     REQUIRE(sum[1] == 0x0000000000000000);
//     REQUIRE(sum[2] == 0x0000000000000000);
//     REQUIRE(sum[3] == 0x0000000000000000);
// }

// TEST_CASE("uint256  add256  1 to negative zero hash  expected", "[uint256 tests]")
// {
//     static const uint256_t value(negative_zero_hash);
//     static auto const sum = value + 1;
//     REQUIRE(sum[0] == 0x0000000000000001);
//     REQUIRE(sum[1] == 0x0000000000000000);
//     REQUIRE(sum[2] == 0x0000000000000000);
//     REQUIRE(sum[3] == 0x8000000000000000);
// }

// // divide256
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  divide256  unit hash by null hash  throws overflow error", "[uint256 tests]")
// {
//     BOOST_REQUIRE_THROWS_AS(uint256_t(unit_hash) / uint256_t(0), std::overflow_error);
// }

// TEST_CASE("uint256  divide256  null hash by unit hash  null hash", "[uint256 tests]")
// {
//     REQUIRE(uint256_t(null_hash) / uint256_t(unit_hash) == uint256_t());
// }

// TEST_CASE("uint256  divide256  max hash by 3  fives hash", "[uint256 tests]")
// {
//     REQUIRE(uint256_t(max_hash) / uint256_t(3) == uint256_t(fives_hash));
// }

// TEST_CASE("uint256  divide256  max hash by max hash  1", "[uint256 tests]")
// {
//     REQUIRE(uint256_t(max_hash) / uint256_t(max_hash) == uint256_t(1));
// }

// TEST_CASE("uint256  divide256  max hash by 256  shifts right 8 bits", "[uint256 tests]")
// {
//     static const uint256_t value(max_hash);
//     static auto const quotient = value / uint256_t(256);
//     REQUIRE(quotient[0] == 0xffffffffffffffff);
//     REQUIRE(quotient[1] == 0xffffffffffffffff);
//     REQUIRE(quotient[2] == 0xffffffffffffffff);
//     REQUIRE(quotient[3] == 0x00ffffffffffffff);
// }

// // increment
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  increment  0  1", "[uint256 tests]")
// {
//     REQUIRE(++uint256_t(0) == uint256_t(1));
// }

// TEST_CASE("uint256  increment  1  2", "[uint256 tests]")
// {
//     REQUIRE(++uint256_t(1) == uint256_t(2));
// }

// TEST_CASE("uint256  increment  max hash  null hash", "[uint256 tests]")
// {
//     REQUIRE(++uint256_t(max_hash) == uint256_t());
// }

// TEST_CASE("uint256  increment  0xffffffff  0x0100000000", "[uint256 tests]")
// {
//     static auto const increment = ++uint256_t(0xffffffff);
//     REQUIRE(increment[0] == 0x0000000100000000);
//     REQUIRE(increment[1] == 0x0000000000000000);
//     REQUIRE(increment[2] == 0x0000000000000000);
//     REQUIRE(increment[3] == 0x0000000000000000);
// }

// TEST_CASE("uint256  increment  negative zero hash  expected", "[uint256 tests]")
// {
//     static auto const increment = ++uint256_t(negative_zero_hash);
//     REQUIRE(increment[0] == 0x0000000000000001);
//     REQUIRE(increment[1] == 0x0000000000000000);
//     REQUIRE(increment[2] == 0x0000000000000000);
//     REQUIRE(increment[3] == 0x8000000000000000);
// }

// // assign32
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign  null hash 0  null hash", "[uint256 tests]")
// {
//     uint256_t value(null_hash);
//     value = 0;
//     REQUIRE(value == uint256_t());
// }

// TEST_CASE("uint256  assign  max hash 0  null hash", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value = 0;
//     REQUIRE(value == uint256_t());
// }

// TEST_CASE("uint256  assign  odd hash to 42  42", "[uint256 tests]")
// {
//     uint256_t value(odd_hash);
//     value = 42;
//     REQUIRE(value[0] == 0x000000000000002a);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x0000000000000000);
// }

// // assign shift right
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign shift right  null hash  null hash", "[uint256 tests]")
// {
//     uint256_t value1;
//     uint256_t value2;
//     uint256_t value3;
//     value1 >>= 0;
//     value2 >>= 1;
//     value3 >>= max_uint32;
//     REQUIRE(value1 == uint256_t());
//     REQUIRE(value2 == uint256_t());
//     REQUIRE(value3 == uint256_t());
// }

// TEST_CASE("uint256  assign shift right  unit hash 0  unit hash", "[uint256 tests]")
// {
//     uint256_t value(unit_hash);
//     value >>= 0;
//     REQUIRE(value == uint256_t(unit_hash));
// }

// TEST_CASE("uint256  assign shift right  unit hash positive  null hash", "[uint256 tests]")
// {
//     uint256_t value1(unit_hash);
//     uint256_t value2(unit_hash);
//     value1 >>= 1;
//     value2 >>= max_uint32;
//     REQUIRE(value1 == uint256_t());
//     REQUIRE(value2 == uint256_t());
// }

// TEST_CASE("uint256  assign shift right  max hash 1  most hash", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value >>= 1;
//     REQUIRE(value == uint256_t(most_hash));
// }

// TEST_CASE("uint256  assign shift right  odd hash 32  expected", "[uint256 tests]")
// {
//     uint256_t value(odd_hash);
//     value >>= 32;
//     REQUIRE(value[0] == 0x23aaaaabbcbcc487);
//     REQUIRE(value[1] == 0x8cd0923434858989);
//     REQUIRE(value[2] == 0x23499ab234bf128e);
//     REQUIRE(value[3] == 0x0000000084373902);
// }

// // assign shift left
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign shift left  null hash  null hash", "[uint256 tests]")
// {
//     uint256_t value1;
//     uint256_t value2;
//     uint256_t value3;
//     value1 <<= 0;
//     value2 <<= 1;
//     value3 <<= max_uint32;
//     REQUIRE(value1 == uint256_t());
//     REQUIRE(value2 == uint256_t());
//     REQUIRE(value3 == uint256_t());
// }

// TEST_CASE("uint256  assign shift left  unit hash 0  1", "[uint256 tests]")
// {
//     uint256_t value(unit_hash);
//     value <<= 0;
//     REQUIRE(value == uint256_t(1));
// }

// TEST_CASE("uint256  assign shift left  unit hash 1  2", "[uint256 tests]")
// {
//     uint256_t value(unit_hash);
//     value <<= 1;
//     REQUIRE(value == uint256_t(2));
// }

// TEST_CASE("uint256  assign shift left  unit hash 31  0x80000000", "[uint256 tests]")
// {
//     uint256_t value(unit_hash);
//     value <<= 31;
//     REQUIRE(value == uint256_t(0x80000000));
// }

// TEST_CASE("uint256  assign shift left  max hash 1  expected", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value <<= 1;
//     REQUIRE(value[0] == 0xfffffffffffffffe);
//     REQUIRE(value[1] == 0xffffffffffffffff);
//     REQUIRE(value[2] == 0xffffffffffffffff);
//     REQUIRE(value[3] == 0xffffffffffffffff);
// }

// TEST_CASE("uint256  assign shift left  odd hash 32  expected", "[uint256 tests]")
// {
//     uint256_t value(odd_hash);
//     value <<= 32;
//     REQUIRE(value[0] == 0x4353fff400000000);
//     REQUIRE(value[1] == 0x23aaaaabbcbcc487);
//     REQUIRE(value[2] == 0x8cd0923434858989);
//     REQUIRE(value[3] == 0x23499ab234bf128e);
// }

// // assign multiply32
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign multiply32  0 by 0  0", "[uint256 tests]")
// {
//     uint256_t value;
//     value *= 0;
//     REQUIRE(value == uint256_t(0));
// }

// TEST_CASE("uint256  assign multiply32  0 by 1  0", "[uint256 tests]")
// {
//     uint256_t value;
//     value *= 1;
//     REQUIRE(value == uint256_t(0));
// }

// TEST_CASE("uint256  assign multiply32  1 by 1  1", "[uint256 tests]")
// {
//     uint256_t value(1);
//     value *= 1;
//     REQUIRE(value == uint256_t(1));
// }

// TEST_CASE("uint256  assign multiply32  42 by 1  42", "[uint256 tests]")
// {
//     uint256_t value(42);
//     value *= 1;
//     REQUIRE(value == uint256_t(42));
// }

// TEST_CASE("uint256  assign multiply32  1 by 42  42", "[uint256 tests]")
// {
//     uint256_t value(1);
//     value *= 42;
//     REQUIRE(value == uint256_t(42));
// }

// TEST_CASE("uint256  assign multiply32  fives hash by 3  max hash", "[uint256 tests]")
// {
//     uint256_t value(fives_hash);
//     value *= 3;
//     REQUIRE(value == uint256_t(max_hash));
// }

// TEST_CASE("uint256  assign multiply32  ones hash by max uint32  max hash", "[uint256 tests]")
// {
//     uint256_t value(ones_hash);
//     value *= max_uint32;
//     REQUIRE(value == uint256_t(max_hash));
// }

// TEST_CASE("uint256  assign multiply32  max hash by 256  shifts left 8 bits", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value *= 256;
//     REQUIRE(value[0] == 0xffffffffffffff00);
//     REQUIRE(value[1] == 0xffffffffffffffff);
//     REQUIRE(value[2] == 0xffffffffffffffff);
//     REQUIRE(value[3] == 0xffffffffffffffff);
// }

// // assign divide32
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign divide32  unit hash by null hash  throws overflow error", "[uint256 tests]")
// {
//     uint256_t value(unit_hash);
//     BOOST_REQUIRE_THROWS_AS(value /= 0, std::overflow_error);
// }

// TEST_CASE("uint256  assign divide32  null hash by unit hash  null hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value /= 1;
//     REQUIRE(value == uint256_t(null_hash));
// }

// TEST_CASE("uint256  assign divide32  max hash by 3  fives hash", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value /= 3;
//     REQUIRE(value == uint256_t(fives_hash));
// }

// TEST_CASE("uint256  assign divide32  max hash by max uint32  ones hash", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value /= max_uint32;
//     REQUIRE(value == uint256_t(ones_hash));
// }

// TEST_CASE("uint256  assign divide32  max hash by 256  shifts right 8 bits", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value /= 256;
//     REQUIRE(value[0] == 0xffffffffffffffff);
//     REQUIRE(value[1] == 0xffffffffffffffff);
//     REQUIRE(value[2] == 0xffffffffffffffff);
//     REQUIRE(value[3] == 0x00ffffffffffffff);
// }

// // assign add256
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign add256  0 to null hash  null hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value += 0;
//     REQUIRE(value == uint256_t(0));
// }

// TEST_CASE("uint256  assign add256  null hash to null hash  null hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value += uint256_t();
//     REQUIRE(uint256_t() + uint256_t() == uint256_t());
// }

// TEST_CASE("uint256  assign add256  1 to max hash  null hash", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value += 1;
//     REQUIRE(value == uint256_t());
// }

// TEST_CASE("uint256  assign add256  ones hash to odd hash  expected", "[uint256 tests]")
// {
//     uint256_t value(odd_hash);
//     value += uint256_t(ones_hash);
//     REQUIRE(value[0] == 0xbcbcc4884353fff5);
//     REQUIRE(value[1] == 0x3485898a23aaaaac);
//     REQUIRE(value[2] == 0x34bf128f8cd09235);
//     REQUIRE(value[3] == 0x8437390323499ab3);
// }

// TEST_CASE("uint256  assign add256  1 to 0xffffffff  0x0100000000", "[uint256 tests]")
// {
//     uint256_t value(0xffffffff);
//     value += 1;
//     REQUIRE(value[0] == 0x0000000100000000);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x0000000000000000);
// }

// TEST_CASE("uint256  assign add256  1 to negative zero hash  expected", "[uint256 tests]")
// {
//     uint256_t value(negative_zero_hash);
//     value += 1;
//     REQUIRE(value[0] == 0x0000000000000001);
//     REQUIRE(value[1] == 0x0000000000000000);
//     REQUIRE(value[2] == 0x0000000000000000);
//     REQUIRE(value[3] == 0x8000000000000000);
// }

// // assign subtract256
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign subtract256  0 from null hash  null hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value -= 0;
//     REQUIRE(value == uint256_t(0));
// }

// TEST_CASE("uint256  assign subtract256  null hash from null hash  null hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value -= uint256_t();
//     REQUIRE(uint256_t() + uint256_t() == uint256_t());
// }

// TEST_CASE("uint256  assign subtract256  1 from null hash  max hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value -= 1;
//     REQUIRE(value == uint256_t(max_hash));
// }

// TEST_CASE("uint256  assign subtract256  1 from max hash  expected", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value -= 1;
//     REQUIRE(value[0] == 0xfffffffffffffffe);
//     REQUIRE(value[1] == 0xffffffffffffffff);
//     REQUIRE(value[2] == 0xffffffffffffffff);
//     REQUIRE(value[3] == 0xffffffffffffffff);
// }

// TEST_CASE("uint256  assign subtract256  ones hash from odd hash  expected", "[uint256 tests]")
// {
//     uint256_t value(odd_hash);
//     value -= uint256_t(ones_hash);
//     REQUIRE(value[0] == 0xbcbcc4864353fff3);
//     REQUIRE(value[1] == 0x3485898823aaaaaa);
//     REQUIRE(value[2] == 0x34bf128d8cd09233);
//     REQUIRE(value[3] == 0x8437390123499ab1);
// }

// TEST_CASE("uint256  assign subtract256  1 from 0xffffffff  0x0100000000", "[uint256 tests]")
// {
//     uint256_t value(0xffffffff);
//     value -= 1;
//     REQUIRE(value == uint256_t(0xfffffffe));
// }

// TEST_CASE("uint256  assign subtract256  1 from negative zero hash  most hash", "[uint256 tests]")
// {
//     uint256_t value(negative_zero_hash);
//     value -= 1;
//     REQUIRE(value == uint256_t(most_hash));
// }

// // assign divide256
// //-----------------------------------------------------------------------------

// TEST_CASE("uint256  assign divide  unit hash by null hash  throws overflow error", "[uint256 tests]")
// {
//     uint256_t value(unit_hash);
//     BOOST_REQUIRE_THROWS_AS(value /= uint256_t(0), std::overflow_error);
// }

// TEST_CASE("uint256  assign divide  null hash by unit hash  null hash", "[uint256 tests]")
// {
//     uint256_t value;
//     value /= uint256_t(unit_hash);
//     REQUIRE(value == uint256_t(null_hash));
// }

// TEST_CASE("uint256  assign divide  max hash by 3  fives hash", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value /= 3;
//     REQUIRE(value == uint256_t(fives_hash));
// }

// TEST_CASE("uint256  assign divide  max hash by max hash  1", "[uint256 tests]")
// {
//     uint256_t value(max_hash);
//     value /= uint256_t(max_hash);
//     REQUIRE(value == uint256_t(1));
// }

// TEST_CASE("uint256  assign divide  max hash by 256  shifts right 8 bits", "[uint256 tests]")
// {
//     static const uint256_t value(max_hash);
//     static auto const quotient = value / uint256_t(256);
//     REQUIRE(quotient[0] == 0xffffffffffffffff);
//     REQUIRE(quotient[1] == 0xffffffffffffffff);
//     REQUIRE(quotient[2] == 0xffffffffffffffff);
//     REQUIRE(quotient[3] == 0x00ffffffffffffff);
// }

