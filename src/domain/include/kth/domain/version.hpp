// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

///////////////////////////////////////////////////////////////////////////////
//
//        GENERATED SOURCE CODE, DO NOT EDIT EXCEPT EXPERIMENTALLY
//
///////////////////////////////////////////////////////////////////////////////
#ifndef KTH_VERSION_HPP
#define KTH_VERSION_HPP

#include <string_view>

namespace kth {

// Version string from build system (conan -> cmake -> C++)
inline constexpr std::string_view version = "0.82.0";

// Currency identifier
#if defined(KTH_CURRENCY_BCH)
inline constexpr std::string_view currency = "BCH";
inline constexpr std::string_view currency_name = "Bitcoin Cash";
#elif defined(KTH_CURRENCY_BTC)
inline constexpr std::string_view currency = "BTC";
inline constexpr std::string_view currency_name = "Bitcoin";
#elif defined(KTH_CURRENCY_LTC)
inline constexpr std::string_view currency = "LTC";
inline constexpr std::string_view currency_name = "Litecoin";
#else
inline constexpr std::string_view currency = "UNKNOWN";
inline constexpr std::string_view currency_name = "Unknown";
#endif

// User agent components
inline constexpr std::string_view client_name = "Knuth";

} // namespace kth

#endif // KTH_VERSION_HPP
