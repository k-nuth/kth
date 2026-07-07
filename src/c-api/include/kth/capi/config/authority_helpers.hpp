// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CONFIG_AUTHORITY_HELPERS_HPP_
#define KTH_CAPI_CONFIG_AUTHORITY_HELPERS_HPP_

#include <kth/capi/config/authority.h>

#include <vector>

#include <asio/ip/address.hpp>

#include <kth/capi/config/helpers.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::capi::helpers {

using authority = kth::infrastructure::config::authority;

// The C API contract requires `x.ip` to be a non-null, well-formed
// IPv4/IPv6 literal; violating that is a caller bug, not a runtime
// input error — so we abort via KTH_CONTRACT instead of silently
// returning a sentinel authority.
inline
authority authority_to_cpp(kth_authority const& x) {
    KTH_CONTRACT(x.ip != nullptr);
    std::error_code ec;
    auto const address = ::asio::ip::make_address(x.ip, ec);
    KTH_CONTRACT( ! ec);
    return authority{address, x.port};
}

inline
kth_authority authority_to_c(authority const& x) {
    return {kth::create_c_str(x.to_hostname()), x.port()};
}

inline
void authority_delete(kth_authority& x) {
    free(x.ip);
}

inline
std::vector<authority> authority_list_to_cpp(kth_authority const* data, size_t n) {
    return kth::capi::helpers::list_to_cpp(data, n, authority_to_cpp);
}

inline
kth_authority* authority_list_to_c(std::vector<authority> const& data, size_t& out_size) {
    return kth::capi::helpers::list_to_c(data, out_size, authority_to_c);
}

inline
void authority_list_delete(kth_authority* list, size_t n) {
    list_c_delete(list, n, authority_delete);
}

}

#endif // KTH_CAPI_CONFIG_AUTHORITY_HELPERS_HPP_
