// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// A separate process guarantees a cold first lookup. Static linking lets the
// test intercept allocations made by the real C API and infrastructure.
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string_view>
#include <type_traits>

#include <kth/capi/error.h>
#include <kth/infrastructure/error.hpp>

namespace {
bool deny_allocations = false;
}

void* operator new(std::size_t n) {
    if (deny_allocations) throw std::bad_alloc{};
    if (auto* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    static_assert(std::is_same_v<decltype(kth::error::message(0)), std::string_view>);
    std::set_terminate([] { std::_Exit(86); });
    deny_allocations = true;
    // Native and C lookups must not allocate, even on their first call.
    auto const native = kth::error::message(kth::error::insufficient_alt_stack);
    if (native != "insufficient alt stack") return 1;
    if (std::strcmp(kth_error_message(kth_ec_insufficient_alt_stack), native.data()) != 0) return 2;
    if (std::strcmp(kth_error_message(INT32_MAX), "invalid code") != 0) return 3;
    if (std::strcmp(kth_error_message(INT32_MIN), "invalid code") != 0) return 4;
    if (std::strcmp(kth_error_message(kth_ec_out_of_memory), "out of memory") != 0) return 5;
    if (std::strcmp(kth_error_message(kth_ec_unhandled_exception), "unhandled exception") != 0) return 6;
    // Check every mapped code and every gap, including the sentinel above them.
    for (int code = 0; code <= kth::error::last_error_code; ++code) {
        auto const view = kth::error::message(code);
        auto const* text = kth_error_message(code);
        if (view.empty() || std::strlen(text) != view.size() || view != text) return 7;
        if (text != kth_error_message(code)) return 8;
    }
    deny_allocations = false;
    // The existing std::error_code API keeps its owning-string contract.
    if (kth::error::make_error_code(kth::error::insufficient_alt_stack).message() != native) return 9;
}
