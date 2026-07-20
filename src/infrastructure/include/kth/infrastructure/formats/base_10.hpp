// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_10_HPP
#define KTH_INFRASTUCTURE_BASE_10_HPP

#include <cstdint>
#include <string>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>

namespace kth {

constexpr uint8_t btc_decimal_places = 8;
constexpr uint8_t mbtc_decimal_places = 5;
constexpr uint8_t ubtc_decimal_places = 2;

/**
 * Validates and parses an amount string according to the BIP 21 grammar.
 * @param decmial_places the location of the decimal point.
 * The default is 0, which treats the input as a normal integer.
 * @param strict true to treat fractional results as an error,
 * or false to round them upwards.
 * @return false for failure.
 */
KI_API bool decode_base10(uint64_t& out, std::string const& amount, uint8_t decimal_places=0, bool strict=true);

/**
 * Writes a Bitcoin amount to a string, following the BIP 21 grammar.
 * Avoids the rounding issues often seen with floating-point methods.
 * @param decmial_places the location of the decimal point.
 * The default is 0, which treats the input as a normal integer.
 */
KI_API std::string encode_base10(uint64_t amount,
    uint8_t decimal_places=0);

// Old names:
KI_API bool btc_to_satoshi(uint64_t& satoshi, std::string const& btc);
KI_API std::string satoshi_to_btc(uint64_t satoshi);

} // namespace kth

#endif

