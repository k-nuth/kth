// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#pragma once
#include <kth/capi/primitives.h>

#include <control.hpp>
namespace kth {
inline kth_error_code_t to_c_err(std::error_code error) {
    return ! error ? kth_ec_success : error == kth::error::service_stopped ? kth_ec_service_stopped
                                                                           : kth_ec_operation_failed;
}
inline kth_hash_t to_hash_t(hash_digest const& hash) {
    kth_hash_t result{};
    std::copy(hash.begin(), hash.end(), result.hash);
    return result;
}
}  // namespace kth
