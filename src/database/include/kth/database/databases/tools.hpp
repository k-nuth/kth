// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_TOOLS_HPP_
#define KTH_DATABASE_TOOLS_HPP_

#include <chrono>

#include <kth/domain.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::database {

//Note: same logic as is_stale()

template <typename Clock>
inline
std::chrono::time_point<Clock> to_time_point(std::chrono::seconds secs) {
    return std::chrono::time_point<Clock>(typename Clock::duration(secs));
}

template <typename Clock>
inline
bool is_old_block_(uint32_t header_ts, std::chrono::seconds limit) {
    return (Clock::now() - to_time_point<Clock>(std::chrono::seconds(header_ts))) >= limit;
}

template <typename Clock>
inline
bool is_old_block_(domain::chain::block const& block, std::chrono::seconds limit) {
    return is_old_block_<Clock>(block.header().timestamp(), limit);
}

constexpr
inline
std::chrono::seconds blocks_to_seconds(uint32_t blocks) {
    return std::chrono::seconds(blocks * target_spacing_seconds);   //10 * 60
}

inline
data_chunk db_value_to_data_chunk(KTH_DB_val const& value) {
    return data_chunk{static_cast<uint8_t*>(kth_db_get_data(value)),
                      static_cast<uint8_t*>(kth_db_get_data(value)) + kth_db_get_size(value)};
}

// Zero-copy read view over an `KTH_DB_val`. Use when the caller needs
// to feed the bytes into a manual `byte_reader` (e.g. because the
// deserializer is a free function rather than `T::from_data` — see
// `get_header_and_abla_state_from_data`, or a hierarchy where the
// concept `Deserializable<T,...>` doesn't quite fit, e.g. `output_point
// : point`). For the common case prefer `from_db_value<T>` directly.
// Same lifetime rule as `from_db_value`: the returned span points at
// store-owned memory that must live until the read is done.
[[nodiscard]] inline
kth::byte_span db_value_as_span(KTH_DB_val const& value) {
    return {static_cast<uint8_t const*>(kth_db_get_data(value)),
            kth_db_get_size(value)};
}

// One-liner that materialises a `byte_reader` positioned at the start
// of an `KTH_DB_val`'s payload. Shortcut for
// `byte_reader{db_value_as_span(value)}` — same zero-copy semantics
// and same txn-lifetime rule (bytes must outlive the reader's use).
[[nodiscard]] inline
kth::byte_reader db_reader(KTH_DB_val const& value) {
    return kth::byte_reader{db_value_as_span(value)};
}

// Write-side symmetric of `db_reader`. Bundles the pattern
//   auto arr = kth::to_data_chunk(obj, args...);
//   auto val = kth_db_make_value(arr.size(), arr.data());
// into one owning wrapper: the caller keeps a single local, the buffer
// lives inside it, and `.val` is the `KTH_DB_val` LMDB expects. The
// two variables had to travel together anyway (the value's `data`
// pointer is only stable while the chunk is alive), so folding them
// into one aggregate is a straight simplification, not a lifetime
// change. Copy is deleted so callers can't accidentally hand out a
// dangling `val`; move re-derives `val` from the moved-into buffer.
struct owned_db_value {
    data_chunk buf;
    KTH_DB_val val;

    explicit owned_db_value(data_chunk&& b)
        : buf(std::move(b))
        , val(kth_db_make_value(buf.size(), buf.data()))
    {}

    owned_db_value(owned_db_value const&) = delete;
    owned_db_value& operator=(owned_db_value const&) = delete;

    owned_db_value(owned_db_value&& o) noexcept
        : buf(std::move(o.buf))
        , val(kth_db_make_value(buf.size(), buf.data()))
    {}

    owned_db_value& operator=(owned_db_value&& o) noexcept {
        buf = std::move(o.buf);
        val = kth_db_make_value(buf.size(), buf.data());
        return *this;
    }
};

// Serialise `obj` into an `owned_db_value` ready to hand to LMDB.
// The buffer lives inside the returned wrapper; pass `&result.val`
// to `kth_db_put` / `kth_db_get` / `kth_db_del` / cursor calls, and
// keep the wrapper alive until the LMDB call returns.
template <typename T, typename... Args>
    requires kth::Serializable<T, Args...>
[[nodiscard]] inline
owned_db_value to_db_value(T const& obj, Args... args) {
    return owned_db_value{kth::to_data_chunk(obj, args...)};
}

// Zero-copy `from_data` over an LMDB value: build a `byte_reader`
// straight over the store-owned memory instead of first copying the
// bytes into a heap `data_chunk`. The caller pattern
//     auto data = db_value_to_data_chunk(value);
//     auto res  = kth::from_data_chunk<T>(data);
// wastes one heap allocation + one linear copy per lookup — hot on
// read paths that walk many entries (block/history/spend/reorg).
//
// `KTH_DB_val` points at memory the LMDB txn owns for the lifetime of
// the read; the returned `expect<T>` must be consumed before that txn
// ends, same as `data_chunk` version.
template <typename T, typename... Args>
    requires kth::Deserializable<T, Args...>
[[nodiscard]] inline
expect<T> from_db_value(KTH_DB_val const& value, Args... args) {
    kth::byte_span span{
        static_cast<uint8_t const*>(kth_db_get_data(value)),
        kth_db_get_size(value)
    };
    kth::byte_reader reader(span);
    return T::from_data(reader, args...);
}

} // namespace kth::database

#endif // KTH_DATABASE_TOOLS_HPP_
