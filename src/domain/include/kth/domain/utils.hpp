// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_UTIL_HPP
#define KTH_DOMAIN_UTIL_HPP

#include <kth/domain/multi_crypto_settings.hpp>

namespace kth {

constexpr inline
bool witness_default() {
#if ! defined(KTH_SEGWIT_ENABLED)
    return false;
#else
    return true;
#endif
}

constexpr inline
#if ! defined(KTH_SEGWIT_ENABLED)
bool witness_val(bool /*x*/) {
    return false;
#else
bool witness_val(bool x) {
    return x;
#endif
}

#if ! defined(KTH_SEGWIT_ENABLED)
#define KTH_DECL_WITN_ARG bool /*witness*/ = false      //NOLINT
#define KTH_DEF_WITN_ARG bool /*witness = false*/       //NOLINT
#else
#define KTH_DECL_WITN_ARG bool witness = false          //NOLINT
#define KTH_DEF_WITN_ARG bool witness /*= false*/       //NOLINT
#endif


//TODO(fernando): move to infrastructure
//C++14
template <typename E>
constexpr auto to_underlying(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}

} // namespace kth

#endif // KTH_DOMAIN_UTIL_HPP
