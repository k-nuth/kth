// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_ERRORS_HPP_
#define KTH_CAPI_DETAIL_ERRORS_HPP_

// C++ error -> `kth_error_code_t` mapping.

#include <system_error>

#include <kth/capi/error.h>

namespace kth {

inline
kth_error_code_t to_c_err(std::error_code const& ec) {
    return static_cast<kth_error_code_t>(ec.value());
}

// Fallback for bare enum error arms (e.g. `kth::error::error_code_t`)
// used by `std::expected<T, EnumT>`. The non-template overload above
// wins for `std::error_code`; this template catches every other shape
// that is convertible to `kth_error_code_t`.
template <typename E>
inline
kth_error_code_t to_c_err(E const& e) {
    return static_cast<kth_error_code_t>(e);
}

} // namespace kth

#endif /* KTH_CAPI_DETAIL_ERRORS_HPP_ */
