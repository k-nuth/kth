// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_OWNERSHIP_HPP_
#define KTH_CAPI_DETAIL_OWNERSHIP_HPP_

// Heap promotion and release of C-API objects. Every owned handle handed to a
// C consumer is created by one of the `to_owned*` / `make_owned*` helpers and
// released by `del` / `del_list` (the generated `kth_*_destruct`). Keeping all
// of them here makes each ownership transfer greppable.

#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <kth/capi/detail/handle_access.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth {

// Concept used by `to_owned(T&&)` to step aside when the argument is a
// `std::shared_ptr`. In that case the caller wants the pointed-to object
// heap-copied (see the `to_owned(std::shared_ptr<T> const&)` overload
// below), not the shared_ptr itself.
namespace detail {
template <typename>                    constexpr bool is_shared_ptr = false;
template <typename T> constexpr bool is_shared_ptr<std::shared_ptr<T>> = true;
} // namespace detail

template <typename T>
concept not_shared_ptr = ! detail::is_shared_ptr<std::remove_cvref_t<T>>;

// ---------------------------------------------------------------------------
// to_owned: promote an existing value to the heap
// ---------------------------------------------------------------------------

// Promote a value to the heap and surrender ownership to the caller,
// which from the C-API perspective is exactly the transfer the consumer
// is expected to release later via the matching `kth_*_destruct`
// function. Forwards into `new T(...)`, so an rvalue is moved and an
// lvalue is copied. The name documents the ownership transfer at every
// call site, making these promotions greppable.
//
// The `not_shared_ptr` constraint keeps this forwarding-reference
// overload out of the picture for `std::shared_ptr` arguments — those
// flow to the dedicated overloads below that unwrap the shared_ptr and
// promote the pointed-to object.
template <not_shared_ptr T>
std::decay_t<T>* to_owned(T&& x) {
    return new std::decay_t<T>(std::forward<T>(x));
}

template <typename T>
inline
std::remove_const_t<T>* to_owned(std::shared_ptr<T> const& ptr) {
    if ( ! ptr) return nullptr;
    return new std::remove_const_t<T>(*ptr);
}

// Rvalue overload: caller has signalled they are done with `ptr`
// (e.g. passed `std::move(...)`), so we can move the pointed-to value
// into the heap copy instead of copying it. If `ptr` still shared
// ownership at the call site, the caller has violated the move
// contract — same rule as `std::move` anywhere else.
template <typename T>
inline
std::remove_const_t<T>* to_owned(std::shared_ptr<T>&& ptr) {
    if ( ! ptr) return nullptr;
    return new std::remove_const_t<T>(std::move(*ptr));
}

// Same as `to_owned`, but gates the promotion on `check_valid(&x)`. The
// validity of the heap copy mirrors the source's validity (the
// constructor doesn't change `operator bool`), so we test `x` BEFORE
// allocating — invalid input returns `nullptr` without ever touching
// the heap. Matches the documented "or NULL on failure" contract that
// generated factories advertise. Sentinel factories whose return is
// intentionally `operator bool == false` should opt out and use `to_owned` instead.
template <typename T>
std::decay_t<T>* to_owned_if_valid(T&& x) {
    if ( ! check_valid(&x)) return nullptr;
    return new std::decay_t<T>(std::forward<T>(x));
}

// Same as `to_owned`, but yields `nullptr` unless `ec` is success.
template <typename T>
inline
std::remove_const_t<T>* to_owned_if_success(std::shared_ptr<T> const& ptr, std::error_code ec) {
    if (ec != kth::error::success) return nullptr;
    return new std::remove_const_t<T>(*ptr);
}

// Rvalue overload: same move-from-`*ptr` rationale as the `to_owned`
// rvalue overload above.
template <typename T>
inline
std::remove_const_t<T>* to_owned_if_success(std::shared_ptr<T>&& ptr, std::error_code ec) {
    if (ec != kth::error::success) return nullptr;
    return new std::remove_const_t<T>(std::move(*ptr));
}

// Non-shared_ptr overload: forwarding-reference so rvalues move and
// lvalues copy, without spelling out two bodies. The `not_shared_ptr`
// constraint keeps this out of the picture when the caller hands over
// a shared_ptr (see the two overloads above).
template <not_shared_ptr T>
inline
std::decay_t<T>* to_owned_if_success(T&& val, std::error_code ec) {
    if (ec != kth::error::success) return nullptr;
    return new std::decay_t<T>(std::forward<T>(val));
}

// ---------------------------------------------------------------------------
// make_owned: construct directly on the heap
// ---------------------------------------------------------------------------

// In-place variant: constructs `T` from the forwarded argument pack
// instead of copy/moving an already-built object. `T` must be given
// explicitly. It is a separate name from `to_owned`, so it never takes
// part in that overload set; `make_owned<T>(x)` always runs
// `new T(x)`, which also covers `explicit` constructors.
template <typename T, typename... Args>
T* make_owned(Args&&... args) {
    return new T(std::forward<Args>(args)...);
}

// Convenience: `make_owned` for a `std::vector<T>` heap allocation.
// Saves spelling out the inner `std::vector<...>` at the call site.
// Forwards every argument straight into the vector constructor, so
// `make_owned_list<T>()`, `make_owned_list<T>(n)`,
// `make_owned_list<T>(n, value)`, `make_owned_list<T>({a, b, c})` and
// `make_owned_list<T>(begin, end)` all work.
template <typename T, typename... Args>
std::vector<T>* make_owned_list(Args&&... args) {
    return new std::vector<T>(std::forward<Args>(args)...);
}

// Copy an opaque handle: deref it back into `T const&` via `cpp_ref`
// and heap-allocate a fresh copy via `make_owned`. Collapses the generated
// copy-ctor body to a one-liner.
template <typename T>
inline T* clone(void const* h) {
    return make_owned<T>(cpp_ref<T>(h));
}

// ---------------------------------------------------------------------------
// Consuming and releasing
// ---------------------------------------------------------------------------

// Consume an owned mutable handle passed by address. The caller's slot is
// nulled immediately and the returned `std::unique_ptr` owns the object, so
// the handle is released whether the surrounding operation succeeds or
// throws. Generated consuming constructors (`*_construct_move`) use this to
// document that the passed handles are always consumed, including on error.
// The caller must have checked `handle != nullptr && *handle != nullptr`.
template <typename T>
inline std::unique_ptr<T> take(void** handle) noexcept {
    auto* object = static_cast<T*>(*handle);
    *handle = nullptr;
    return std::unique_ptr<T>(object);
}

// Hold a freshly promoted object (`to_owned` / `make_owned` result) in a
// `std::unique_ptr` while a wrapper finishes converting its other outputs. A
// wrapper that has to promote more than one object stages each of them with
// this and publishes the handles (`release()`) only after the last conversion
// that can throw, so an exception frees everything already staged and the
// caller sees no output at all. Wrapping the raw pointer cannot throw.
template <typename T>
inline std::unique_ptr<T> stage_owned(T* object) noexcept {
    return std::unique_ptr<T>(object);
}

// Inverse of `to_owned` / `make_owned` / `make_owned_list`: hands an opaque C
// handle back to `delete`. Null-safe, so generated destructors can call this
// unconditionally without a separate guard — passing `nullptr` is a
// no-op. `del_list<T>(h)` is the matching shortcut for vector handles.
template <typename T>
inline void del(void* h) {
    if (h == nullptr) return;
    delete &cpp_ref<T>(h);
}
template <typename T>
inline void del_list(void* h) {
    if (h == nullptr) return;
    delete &cpp_ref<std::vector<T>>(h);
}

} // namespace kth

#endif /* KTH_CAPI_DETAIL_OWNERSHIP_HPP_ */
