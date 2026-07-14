// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PSEUDO_RANDOM_HPP
#define KTH_INFRASTRUCTURE_PSEUDO_RANDOM_HPP

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth {

namespace detail {

/// Trait behind `randomizable`. It is a trait rather than a plain concept so
/// that the array case can recurse on the element type: concepts cannot be
/// partially specialized, and "trivially copyable with no padding" is not a
/// sufficient test on its own -- `std::array<bool, N>` passes it (bool is one
/// byte, so the array has no padding) while still being undefined to fill.
template <typename T>
struct is_randomizable : std::bool_constant<
    std::integral<T> &&
    ! std::same_as<std::remove_cv_t<T>, bool> &&
    std::has_unique_object_representations_v<T>> {};

/// An array is drawable whole exactly when its element is, which composes:
/// `std::array<std::array<uint8_t, 4>, 2>` follows, `std::array<bool, 4>` does
/// not.
template <typename T, size_t N>
struct is_randomizable<std::array<T, N>> : is_randomizable<std::remove_cv_t<T>> {};

} // namespace detail

/// A type that can be drawn whole from random bytes: every bit pattern of its
/// object representation has to be a value it can legitimately hold.
///
/// Integers qualify, and arrays of them. `bool` does not -- only 0 and 1 are
/// valid, so a random byte is undefined behaviour -- and neither does anything
/// carrying padding bits, whose padding would be filled with entropy that
/// comparison never reads.
template <typename T>
concept randomizable = detail::is_randomizable<std::remove_cv_t<T>>::value;

/// A range whose elements can be overwritten with random bytes in place.
template <typename R>
concept randomizable_range =
    std::ranges::contiguous_range<R> &&
    std::ranges::sized_range<R> &&
    std::is_trivially_copyable_v<std::ranges::range_value_t<R>>;

/// The system CSPRNG.
///
/// Nothing here reports an error, because for our usage there is no recoverable
/// one. We ask the kernel with flags=0, which *blocks* rather than failing when
/// the entropy pool is not ready yet. That leaves exactly three ways out:
///
///   EINTR   a signal hit us while blocking -- transient, so `fill_bytes` retries.
///   EINVAL  bad flags. We pass 0. Reaching it means a bug here, not in the caller.
///   ENOSYS  the kernel has no getrandom (< 3.17). Constant for the life of the
///           process: if it works once it works always.
///
/// Only ENOSYS is environmental, and being constant it is answerable once, at
/// startup, where refusing to boot is a real option -- see `check_available`.
/// Past that point a draw cannot fail, so `generate` returns a plain value and
/// callers need no error path for a condition none of them could act on.
struct KI_API pseudo_random {
    /// Probe the system CSPRNG. Call once, early, from the node's startup path:
    /// this is the only place where an unusable CSPRNG is both detectable and
    /// actionable. Returns the errno as a system_category code, or success.
    [[nodiscard]]
    static
    code check_available() noexcept;

    /// Fill a byte span. This is the only primitive: everything below routes
    /// through it, so the retry lives in exactly one place.
    ///
    /// PRECONDITION: `check_available()` has returned success in this process.
    /// It is not asserted -- re-probing on every draw would cost a syscall to
    /// re-answer a constant. When violated on a machine with no CSPRNG, this
    /// aborts rather than returning bytes that are not random. That is the only
    /// safe move: the callers are generating keys, nonces and salts, and every
    /// one of them is worse off with predictable output than with a crash.
    ///
    /// It deliberately does NOT share a name with the overloads below.
    /// `as_writable_bytes` on a fixed-extent span yields a fixed-extent span,
    /// which binds to a `randomizable_range` template exactly while reaching
    /// this signature only by conversion -- so an overload named `fill` here
    /// would silently lose to the template and recurse forever.
    static
    void fill_bytes(std::span<std::byte> out) noexcept;

    /// Fill a contiguous range in place.
    template <randomizable_range R>
    static
    void fill(R&& out) {
        fill_bytes(std::as_writable_bytes(std::span{out}));
    }

    /// Draw a random value:
    ///     auto const nonce = pseudo_random::generate<uint64_t>();
    ///     auto const salt  = pseudo_random::generate<byte_array<16>>();
    template <randomizable T>
    [[nodiscard]]
    static
    T generate() {
        T value;
        fill_bytes(std::as_writable_bytes(std::span<T, 1>{&value, 1}));
        return value;
    }

    /// Draw a random value uniformly over the closed interval [lo, hi].
    ///
    /// Rejection-sampled rather than `generate<T>() % span`, which is only
    /// uniform when the span happens to divide 2^bits: otherwise the low end of
    /// the interval is over-represented, by up to a factor of two as the span
    /// approaches the width of the type.
    template <randomizable T>
    [[nodiscard]]
    static
    T generate(T lo, T hi) {
        KTH_CONTRACT(lo <= hi);
        using unsigned_t = std::make_unsigned_t<T>;
        constexpr auto max = std::numeric_limits<unsigned_t>::max();

        // Offsets are computed unsigned: hi - lo overflows for a signed T
        // spanning both halves of its range.
        auto const range = unsigned_t(unsigned_t(hi) - unsigned_t(lo));
        if (range == max) {
            return T(generate<unsigned_t>());   // whole width; nothing to reject
        }

        auto const span = unsigned_t(range + 1u);

        // Discard the first (2^bits % span) values, leaving a count that the
        // span divides exactly, so every residue is equally likely.
        auto const floor = unsigned_t(unsigned_t(max - span + 1u) % span);
        unsigned_t draw;
        do {
            draw = generate<unsigned_t>();
        } while (draw < floor);

        return T(unsigned_t(unsigned_t(lo) + unsigned_t(draw % span)));
    }

    /// Shuffle a range.
    ///
    /// Buffered: a std::shuffle asks its generator once per element, so an
    /// unbuffered CSPRNG would cost one syscall per element.
    template <std::ranges::random_access_range R>
        requires std::permutable<std::ranges::iterator_t<R>>
    static
    void shuffle(R&& out) {
        buffered_engine engine;
        std::ranges::shuffle(out, engine);
    }

    /// Overwrite a byte span with zeros, and actually do it.
    ///
    /// `std::fill(p, p + n, 0)` or `arr.fill(0)` over memory nothing reads
    /// again is dead-store elimination bait: the compiler is entitled to drop
    /// the write, and at -O2 it does -- so the usual "erase the secret before
    /// it goes out of scope" line compiles to nothing at all. This routes to a
    /// libc primitive that is contractually not elidable, falling back to a
    /// memset behind a barrier that makes the compiler assume something it
    /// cannot see may read those bytes.
    ///
    /// Same naming rule as fill_bytes/fill, and for the same reason: a `wipe`
    /// overload taking a span here would lose overload resolution to the
    /// template below and recurse.
    static
    void wipe_bytes(std::span<std::byte> out) noexcept;

    /// Overwrite a contiguous range with zeros:
    ///     pseudo_random::wipe(secret);
    template <randomizable_range R>
    static
    void wipe(R&& out) noexcept {
        wipe_bytes(std::as_writable_bytes(std::span{out}));
    }

private:
    /// UniformRandomBitGenerator over the CSPRNG, refilled a block at a time.
    struct buffered_engine {
        using result_type = uint64_t;

        static constexpr result_type min() { return 0; }
        static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }

        ~buffered_engine() {
            // Leftover entropy is not secret in itself, but it is entropy the
            // process is done with; do not leave it on the stack.
            wipe(buffer_);
        }

        result_type operator()() {
            if (next_ == buffer_.size()) {
                fill(buffer_);
                next_ = 0;
            }
            return buffer_[next_++];
        }

    private:
        std::array<result_type, 64> buffer_{};
        size_t next_{buffer_.size()};   // empty, so the first call refills
    };
};

} // namespace kth

#endif
