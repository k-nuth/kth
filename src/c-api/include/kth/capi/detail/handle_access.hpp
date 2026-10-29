// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_HANDLE_ACCESS_HPP_
#define KTH_CAPI_DETAIL_HANDLE_ACCESS_HPP_

// Borrowed access to opaque C handles: handle -> C++ reference, validity
// checks, comparisons through two handles, and C scalar bridging. Nothing here
// transfers ownership or deallocates (`optional_cpp_ref` copies the value into
// an optional, which may itself allocate); ownership lives in
// `detail/ownership.hpp`.

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>

#include <kth/capi/handles.h>

namespace kth {

// Validity check for owned opaque handles returned by constructors and
// factory methods. Prefers an explicit `T::valid()` when the type
// exposes one, falls back to `operator bool` for the types that
// still expose it, and treats everything else as valid.
template<typename T>
inline bool check_valid(T* obj) {
    if constexpr (requires (T const& t) { { t.valid() } -> std::same_as<bool>; }) {
        return obj->valid();
    } else if constexpr (std::is_constructible_v<bool, T const&>) {
        return static_cast<bool>(static_cast<T const&>(*obj));
    } else {
        return true;
    }
}

// Short alias for the `*static_cast<T*>(handle)` pattern used everywhere
// an opaque C handle is crossed back into a C++ reference. The const
// overload kicks in automatically when the handle is a `void const*`.
template<typename T> inline T&       cpp_ref(void* h) noexcept       { return *static_cast<T*>(h); }
template<typename T> inline T const& cpp_ref(void const* h) noexcept { return *static_cast<T const*>(h); }

// Companion for `std::optional<T>` parameters: NULL on the C side is
// `std::nullopt`, any non-null handle is wrapped into an engaged
// optional carrying a value copy. Keeps the optional-param call site
// a single expression instead of the inline ternary spellout.
template<typename T>
inline std::optional<T> optional_cpp_ref(void const* h) {
    return h == nullptr ? std::nullopt : std::optional<T>(cpp_ref<T>(h));
}

inline
int bool_to_int(bool x) noexcept {
    return x;
}

inline
bool int_to_bool(int x) noexcept {
    return x;
}

// Equality through two opaque handles of the same type. Saves the
// double `cpp_ref<T>(...)` spelling and the outer `bool_to_int` wrap
// at every `_equals` call site.
template <typename T>
inline kth_bool_t eq(void const* a, void const* b) {
    return bool_to_int(cpp_ref<T>(a) == cpp_ref<T>(b));
}

// Inequality counterpart for `_not_equal` bindings. Uses the
// compiler-synthesized `!=` rewrite (C++20 rewrites `a != b` to
// `!(a == b)` when only `operator==` is defined), so any T that
// exposes `_equals` also picks this up automatically.
template <typename T>
inline kth_bool_t ne(void const* a, void const* b) {
    return bool_to_int(cpp_ref<T>(a) != cpp_ref<T>(b));
}

// Strict-less-than through two opaque handles of the same type.
// Counterpart to `eq` for `_less` bindings generated from `operator<`.
template <typename T>
inline kth_bool_t lt(void const* a, void const* b) {
    return bool_to_int(cpp_ref<T>(a) < cpp_ref<T>(b));
}

// Strict-greater-than counterpart for `_greater` bindings. Uses the
// compiler-synthesized `>` from `operator<=>` when only the spaceship
// is defined.
template <typename T>
inline kth_bool_t gt(void const* a, void const* b) {
    return bool_to_int(cpp_ref<T>(a) > cpp_ref<T>(b));
}

// Less-or-equal counterpart for `_less_or_equal` bindings.
template <typename T>
inline kth_bool_t le(void const* a, void const* b) {
    return bool_to_int(cpp_ref<T>(a) <= cpp_ref<T>(b));
}

// Greater-or-equal counterpart for `_greater_or_equal` bindings.
template <typename T>
inline kth_bool_t ge(void const* a, void const* b) {
    return bool_to_int(cpp_ref<T>(a) >= cpp_ref<T>(b));
}

// `cpp_ref<std::vector<T>>(h)` shorthand. Same overload resolution
// trick as `cpp_ref`: the const handle picks the const reference.
template <typename T> inline std::vector<T>&       list_ref(void* h) noexcept       { return cpp_ref<std::vector<T>>(h); }
template <typename T> inline std::vector<T> const& list_ref(void const* h) noexcept { return cpp_ref<std::vector<T>>(h); }

// Shorthand for `static_cast<size_t>(x)` when crossing the FFI
// boundary (where sizes come in as `kth_size_t` / signed integers).
template <typename T>
inline std::size_t sz(T x) noexcept(noexcept(static_cast<std::size_t>(x))) { return static_cast<std::size_t>(x); }

template <typename T>
inline
T* ref_to_c(T& x) noexcept(noexcept(&x)) {
    return &x;
}

template <typename T>
inline
T const* ref_to_c(T const& x) noexcept(noexcept(&x)) {
    return &x;
}

} // namespace kth

#endif /* KTH_CAPI_DETAIL_HANDLE_ACCESS_HPP_ */
