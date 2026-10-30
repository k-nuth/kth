// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_ENUM_CAST_HPP_
#define KTH_CAPI_DETAIL_ENUM_CAST_HPP_

// Generic enum <-> integer cast. Knows nothing about any concrete enum; the
// per-enum conversions that use it are generated into `kth/capi/conversions/`.

namespace kth {

// Cast between an integral and an enum, or the reverse. This is a plain
// `static_cast`: it does not validate that the value names an enumerator.
// Generated per-enum helpers provide validation and defaults where required.
template <typename E, typename T>
inline constexpr E ecast(T x) noexcept(noexcept(static_cast<E>(x))) {
    return static_cast<E>(x);
}

} // namespace kth

#endif /* KTH_CAPI_DETAIL_ENUM_CAST_HPP_ */
