// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/machine/number.hpp>

#include <cstdint>

namespace kth::infrastructure::machine {

uint8_t constexpr number::positive_0; // = 0;
uint8_t constexpr number::positive_1; //  = 1;
uint8_t constexpr number::positive_2; //  = 2;
uint8_t constexpr number::positive_3; //  = 3;
uint8_t constexpr number::positive_4; //  = 4;
uint8_t constexpr number::positive_5; //  = 5;
uint8_t constexpr number::positive_6; //  = 6;
uint8_t constexpr number::positive_7; //  = 7;
uint8_t constexpr number::positive_8; //  = 8;
uint8_t constexpr number::positive_9; //  = 9;
uint8_t constexpr number::positive_10; //  = 10;
uint8_t constexpr number::positive_11; //  = 11;
uint8_t constexpr number::positive_12; //  = 12;
uint8_t constexpr number::positive_13; //  = 13;
uint8_t constexpr number::positive_14; //  = 14;
uint8_t constexpr number::positive_15; //  = 15;
uint8_t constexpr number::positive_16; //  = 16;

uint8_t constexpr number::negative_mask; //  = 0x80;
uint8_t constexpr number::negative_1; //  = negative_mask | positive_1;
uint8_t constexpr number::negative_0; //  = negative_mask | positive_0;


// uint8_t constexpr number::positive_0 = 0;
// uint8_t constexpr number::positive_1 = 1;
// uint8_t constexpr number::positive_2 = 2;
// uint8_t constexpr number::positive_3 = 3;
// uint8_t constexpr number::positive_4 = 4;
// uint8_t constexpr number::positive_5 = 5;
// uint8_t constexpr number::positive_6 = 6;
// uint8_t constexpr number::positive_7 = 7;
// uint8_t constexpr number::positive_8 = 8;
// uint8_t constexpr number::positive_9 = 9;
// uint8_t constexpr number::positive_10 = 10;
// uint8_t constexpr number::positive_11 = 11;
// uint8_t constexpr number::positive_12 = 12;
// uint8_t constexpr number::positive_13 = 13;
// uint8_t constexpr number::positive_14 = 14;
// uint8_t constexpr number::positive_15 = 15;
// uint8_t constexpr number::positive_16 = 16;

// uint8_t constexpr number::negative_mask = 0x80;
// uint8_t constexpr number::negative_1 = negative_mask | positive_1;
// uint8_t constexpr number::negative_0 = negative_mask | positive_0;

} // namespace kth::infrastructure::machine
