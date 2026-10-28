// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_BUFFERS_HPP_
#define KTH_CAPI_DETAIL_BUFFERS_HPP_

// Allocation and serialization of C-owned buffers and strings (malloc/free
// side of the ABI, released with `kth_core_destruct_array` and friends).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <span>
#include <string>

#include <kth/infrastructure/utility/assert.hpp>
// byte_writer.hpp uses `expect` without including its definition.
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace kth {

#if defined(_WIN32)
using kth_string_t = std::wstring;
#else
using kth_string_t = std::string;
#endif

template <typename T>
inline
T* mnew(std::size_t n = 1) {
    return static_cast<T*>(malloc(sizeof(T) * n));
}

inline
std::size_t c_str_len(char const* str) {
    return std::strlen(str);
}

inline
std::size_t c_str_len(wchar_t const* str) {
    return std::wcslen(str);
}

inline
char* c_str_cpy(char* dest, char const* src) {
    return strcpy(dest, src);
}

inline
wchar_t* c_str_cpy(wchar_t* dest, wchar_t const* src) {
    return wcscpy(dest, src);
}

template <typename CharT>
inline
CharT* allocate_and_copy_c_str(CharT const* str) {
    auto size = c_str_len(str);
    auto* c_str = mnew<CharT>(size + 1);
    c_str_cpy(c_str, str);
    return c_str;
}

template <typename StrT>
inline
auto* create_c_str(StrT const& str) {
    using CharT = typename StrT::value_type;
    auto* c_str = mnew<CharT>(str.size() + 1);
    std::copy_n(str.data(), str.size(), c_str);
    c_str[str.size()] = CharT{};  // null-terminate (works for string_view too)
    return c_str;
}

template <typename CharT>
inline
std::basic_string<CharT> create_cpp_str(CharT const* str) {
    return std::basic_string<CharT>(str);
}

template <typename N>
inline
uint8_t* create_c_array(kth::data_chunk const& arr, N& out_size) {
    // Allocate at least 1 byte so the returned pointer is non-NULL
    // even when `arr.size() == 0`. `malloc(0)` is implementation-
    // defined: some platforms return NULL, which would make
    // present-but-empty buffers indistinguishable from an absent-key
    // signal at call sites that use NULL to mean "not found" (e.g.
    // map `at()` accessors). The caller still reads only
    // `out_size == arr.size()` bytes; `kth_core_destruct_array` frees
    // the buffer regardless of size.
    auto const n = arr.size();
    auto* ret = mnew<uint8_t>(n != 0 ? n : 1);
    out_size = n;
    std::copy_n(arr.begin(), n, ret);
    return ret;
}

// Fused variant of `to_data_chunk(obj, args...)` + `create_c_array(chunk, out_size)`.
// The naive pipeline is `data_chunk → uint8_t*` and does two heap allocations
// (the vector, then the C buffer) plus one byte-for-byte copy. Fold both
// into a single `malloc(serialized_size)` and serialize straight into the
// C-owned buffer; the C-API `to_data` wrappers hit this once per exported
// object so removing the intermediate vector matters.
template <typename N, typename T, typename... Args>
    requires kth::Serializable<T, Args...>
inline
uint8_t* to_c_array_from(T const& obj, N& out_size, Args... args) {
    auto const n = obj.serialized_size(args...);
    // Same non-NULL sentinel as `create_c_array` — see comment there.
    auto* ret = mnew<uint8_t>(n != 0 ? n : 1);
    out_size = n;
    kth::byte_writer writer(std::span<uint8_t>{ret, n});
    auto const r = obj.to_data(writer, args...);
    // Failure here would ship a partially-initialised buffer over the C
    // ABI (garbage past `writer.position()`). `KTH_CONTRACT` stays live
    // in Release so we abort instead of returning a broken payload.
    KTH_CONTRACT(r.has_value());
    KTH_CONTRACT(writer.position() == n);
    return ret;
}

} // namespace kth

#endif /* KTH_CAPI_DETAIL_BUFFERS_HPP_ */
