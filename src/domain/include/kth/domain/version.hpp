// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_VERSION_HPP
#define KTH_VERSION_HPP

#include <string_view>

namespace kth {

// The version string is defined in `src/version.cpp` (generated per build
// from `src/version.cpp.in`) so it doesn't invalidate ccache for every
// translation unit that transitively includes this header. A bare declaration
// here keeps the API stable; the linker resolves it against the one TU that
// carries the build-time value.
extern std::string_view const version;

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
