// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#pragma once
#include <kth/capi/primitives.h>
#include <system_error>
namespace kth {
template<class T> T& cpp_ref(void* pointer) { return *static_cast<T*>(pointer); }
template<class T> T to_c_struct(T value) { return value; }
inline kth_error_code_t to_c_err(std::error_code error) {
    return error ? kth_ec_operation_failed : kth_ec_success;
}
}
