// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstdlib>
#include <cstring>

#include <kth/capi/data_stack.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace {
using cpp_t = kth::data_stack;
} // namespace

extern "C" {

kth_data_stack_mut_t kth_core_data_stack_construct_default(void) {
    return kth::leak<cpp_t>();
}

void kth_core_data_stack_destruct(kth_data_stack_mut_t list) {
    kth::del<cpp_t>(list);
}

kth_data_stack_mut_t kth_core_data_stack_copy(kth_data_stack_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return kth::clone<cpp_t>(list);
}

kth_size_t kth_core_data_stack_count(kth_data_stack_const_t list) {
    KTH_PRECONDITION(list != nullptr);
    return static_cast<kth_size_t>(kth::cpp_ref<cpp_t>(list).size());
}

void kth_core_data_stack_push_back(kth_data_stack_mut_t list, uint8_t const* data, kth_size_t n) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto& vec = kth::cpp_ref<cpp_t>(list);
    if (n == 0) {
        vec.emplace_back();
    } else {
        vec.emplace_back(data, data + n);
    }
}

uint8_t const* kth_core_data_stack_nth(kth_data_stack_const_t list, kth_size_t n, kth_size_t* out_size) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& vec = kth::cpp_ref<cpp_t>(list);
    KTH_PRECONDITION(kth::sz(n) < vec.size());
    auto const& elem = vec[kth::sz(n)];
    *out_size = static_cast<kth_size_t>(elem.size());
    return elem.data();
}

uint8_t* kth_core_data_stack_nth_copy(kth_data_stack_const_t list, kth_size_t n, kth_size_t* out_size) {
    KTH_PRECONDITION(list != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& vec = kth::cpp_ref<cpp_t>(list);
    KTH_PRECONDITION(kth::sz(n) < vec.size());
    auto const& elem = vec[kth::sz(n)];
    auto const elem_size = elem.size();
    *out_size = static_cast<kth_size_t>(elem_size);
    // Match the rest of the C-API's ownership contract: owned buffers
    // are released with `kth_core_destruct_array`, which is a thin
    // wrapper over `free()`. Allocate with `malloc` so the two pair up.
    auto* buf = static_cast<uint8_t*>(std::malloc(elem_size == 0 ? 1 : elem_size));
    if (buf == nullptr) return nullptr;
    if (elem_size != 0) {
        std::memcpy(buf, elem.data(), elem_size);
    }
    return buf;
}

void kth_core_data_stack_erase(kth_data_stack_mut_t list, kth_size_t n) {
    KTH_PRECONDITION(list != nullptr);
    auto& vec = kth::cpp_ref<cpp_t>(list);
    KTH_PRECONDITION(kth::sz(n) < vec.size());
    vec.erase(vec.begin() + kth::sz(n));
}

} // extern "C"
