// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_ALGORITHM_BINARY_COUNTER_HPP
#define KTH_DOMAIN_ALGORITHM_BINARY_COUNTER_HPP

// Binary Counter Reduction Algorithm
//
// Based on the work of Alexander Stepanov, as described in:
// - "Elements of Programming" by Alexander Stepanov and Paul McJones
// - "From Mathematics to Generic Programming" by Alexander Stepanov and Daniel Rose
//
// C++23 implementation inspired by:
// - https://github.com/tao-cpp/algorithm/blob/master/include/tao/algorithm/binary_counter/add_to_counter.hpp
// - https://github.com/tao-cpp/algorithm/blob/master/include/tao/algorithm/counter_machine.hpp
//
// The binary counter is a technique for efficiently reducing a sequence of values
// using an associative binary operation. Instead of storing all intermediate results
// (O(n) space), it uses a logarithmic number of "digits" (O(log n) space).
//
// Each position k in the counter holds a value that represents 2^k combined elements:
// - Position 0: single element (2^0 = 1)
// - Position 1: two combined elements (2^1 = 2)
// - Position k: 2^k combined elements
//
// When adding a new element:
// 1. If position 0 is empty (zero), store the element there
// 2. If position 0 is full, combine with existing, clear position 0, carry to position 1
// 3. Repeat until finding an empty slot
//
// This is analogous to binary addition with carry propagation.

#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace kth::domain::algorithm {

// =============================================================================
// Concepts
// =============================================================================

template <typename T>
concept Semiregular = std::copyable<T> && std::default_initializable<T>;

template <typename Op, typename T>
concept BinaryOperation = requires(Op op, T a, T b) {
    { op(a, b) } -> std::convertible_to<T>;
};

template <typename T, typename Zero>
concept HasZero = requires(T a, Zero zero) {
    { a == zero } -> std::convertible_to<bool>;
};

// =============================================================================
// add_to_counter - Core algorithm
// =============================================================================

// Adds a value to a binary counter represented as an iterator range.
// Returns an iterator past the last non-zero element, or the carry value
// if the counter overflows.
//
// Preconditions:
// - [first, last) is a valid range
// - op is an associative binary operation
// - zero is the identity element for op (or a sentinel for "empty slot")
//
// Postconditions:
// - If x was absorbed into the counter, returns {iterator past last non-zero, zero}
// - If counter overflowed, returns {last, carry_value}

template <std::forward_iterator I, BinaryOperation<std::iter_value_t<I>> Op>
    requires std::indirectly_writable<I, std::iter_value_t<I>>
constexpr auto add_to_counter(I first, I last, Op op, std::iter_value_t<I> x, std::iter_value_t<I> const& zero)
    -> std::pair<I, std::iter_value_t<I>>
{
    if (x == zero) {
        return {first, zero};
    }

    while (first != last) {
        if (*first == zero) {
            *first = x;
            ++first;
            return {first, zero};
        }
        x = op(*first, x);
        *first = zero;
        ++first;
    }

    // Counter overflow - return carry
    return {last, x};
}

// Unguarded version - assumes there's always space in the counter
// Use only when you can guarantee the counter won't overflow
template <std::forward_iterator I, BinaryOperation<std::iter_value_t<I>> Op>
    requires std::indirectly_writable<I, std::iter_value_t<I>>
constexpr I add_to_counter_unguarded(I first, Op op, std::iter_value_t<I> x, std::iter_value_t<I> const& zero) {
    if (x == zero) {
        return first;
    }

    while (*first != zero) {
        x = op(*first, x);
        *first = zero;
        ++first;
    }

    *first = x;
    ++first;
    return first;
}

// =============================================================================
// reduce_counter - Combine remaining values
// =============================================================================

// Reduces all non-zero values in the counter to a single value.
// Used after all elements have been added to produce the final result.
// Requires an ASSOCIATIVE binary operation.
template <std::forward_iterator I, BinaryOperation<std::iter_value_t<I>> Op>
constexpr std::iter_value_t<I> reduce_counter(I first, I last, Op op, std::iter_value_t<I> const& zero) {
    // Find first non-zero
    while (first != last && *first == zero) {
        ++first;
    }

    if (first == last) {
        return zero;
    }

    auto result = *first;
    ++first;

    while (first != last) {
        if (*first != zero) {
            result = op(*first, result);
        }
        ++first;
    }

    return result;
}

// =============================================================================
// reduce_counter_merkle - Merkle tree reduction with odd duplication
// =============================================================================
//
// Specialized reduce for Bitcoin merkle trees. Unlike standard reduce_counter,
// this handles the case where hash concatenation is NOT associative:
//   H(H(a||b)||c) ≠ H(a||H(b||c))
//
// Bitcoin merkle trees require level-by-level processing where odd elements
// at each level are duplicated. The binary counter encodes partial results
// at each level (position k holds 2^k combined hashes).
//
// Algorithm:
// - Iterate through counter positions from first to last_non_zero
// - If a position has a solo element (not at last position):
//   → Duplicate it: x = op(x, x)  [simulates odd element at that level]
//   → Propagate to next position (carry)
// - When meeting another element during propagation: combine and continue
// - Result ends up at the last non-zero position
//
// Example with 5 elements [a,b,c,d,e]:
//   After adding all: counter = [e, zero, abcd]
//   Position 0: e is solo, not last → x = H(e,e), propagate
//   Position 1: zero → store ee, continue
//   Counter = [zero, ee, abcd]
//   Position 1: ee is solo, not last → x = H(ee,ee), propagate
//   Position 2: has abcd → x = H(abcd, eeee)
//   Result: H(abcd, eeee) ✓
//
// Note: last_non_zero must point to the last non-zero element in the counter.
// The caller (binary_counter_merkle) tracks this during add operations.

template <std::forward_iterator I, BinaryOperation<std::iter_value_t<I>> Op>
    requires std::indirectly_writable<I, std::iter_value_t<I>>
constexpr std::iter_value_t<I> reduce_counter_merkle(I first, I last_non_zero, Op op, std::iter_value_t<I> const& zero) {
    if (first == last_non_zero) {
        // Single position - return it (or zero if empty)
        return *first;
    }

    auto it = first;
    while (it != last_non_zero) {
        if (*it != zero) {
            // Found a solo element that is not at the last position
            // Duplicate it and propagate (like Bitcoin merkle odd duplication)
            auto x = op(*it, *it);
            *it = zero;
            ++it;

            // Propagate with carry (like add_to_counter)
            while (it != last_non_zero && *it != zero) {
                x = op(*it, x);
                *it = zero;
                ++it;
            }

            if (it != last_non_zero) {
                // Found empty slot before last - store and continue
                *it = x;
            } else {
                // Reached last position - combine with it
                *last_non_zero = op(*last_non_zero, x);
            }
        } else {
            ++it;
        }
    }

    return *last_non_zero;
}

// =============================================================================
// binary_counter - High-level counter machine
// =============================================================================

// A counter machine that accumulates values using a binary counter.
// Template parameters:
// - T: the value type (must be Semiregular)
// - Op: associative binary operation on T
// - Size: number of counter positions (default 64, supporting up to 2^64-1 elements)
template <Semiregular T, BinaryOperation<T> Op, std::size_t Size = 64>
class binary_counter {
public:
    constexpr binary_counter(Op op, T zero)
        : op_(op)
        , zero_(zero)
        , count_(0)
    {
        counter_.fill(zero_);
    }

    // Non-copyable, non-movable (maintains internal state)
    binary_counter(binary_counter const&) = delete;
    binary_counter& operator=(binary_counter const&) = delete;
    binary_counter(binary_counter&&) = delete;
    binary_counter& operator=(binary_counter&&) = delete;

    // Add a value to the counter
    // Precondition: must not be called more than 2^Size - 1 times
    constexpr void add(T x) {
        auto [new_end, carry] = add_to_counter(
            counter_.begin(),
            counter_.begin() + count_ + 1,
            op_, x, zero_
        );

        count_ = static_cast<std::size_t>(new_end - counter_.begin());

        // Handle carry (should not happen if Size is large enough)
        if (carry != zero_) {
            if (count_ < Size) {
                counter_[count_] = carry;
                ++count_;
            }
            // else: overflow, value is lost (precondition violated)
        }
    }

    // Get the final reduced value
    [[nodiscard]]
    constexpr T reduce() const {
        return reduce_counter(counter_.begin(), counter_.begin() + count_, op_, zero_);
    }

    // Reset the counter to empty state
    constexpr void reset() {
        for (std::size_t i = 0; i < count_; ++i) {
            counter_[i] = zero_;
        }
        count_ = 0;
    }

    [[nodiscard]]
    constexpr std::size_t size() const { return count_; }

    [[nodiscard]]
    constexpr bool empty() const { return count_ == 0; }

private:
    Op op_;
    T zero_;
    std::array<T, Size> counter_;
    std::size_t count_;
};

// =============================================================================
// binary_counter_merkle - Counter machine for merkle tree computation
// =============================================================================

// Specialized counter for Bitcoin merkle tree computation.
// Uses reduce_counter_merkle which handles odd duplication at each level.
//
// Template parameters:
// - T: the value type (must be Semiregular)
// - Op: binary operation on T (concatenate and hash)
// - Size: number of counter positions (default 64, supporting up to 2^64-1 elements)
template <Semiregular T, BinaryOperation<T> Op, std::size_t Size = 64>
class binary_counter_merkle {
public:
    constexpr binary_counter_merkle(Op op, T zero)
        : op_(op)
        , zero_(zero)
        , last_non_zero_idx_(0)
        , has_elements_(false)
    {
        counter_.fill(zero_);
    }

    // Non-copyable, non-movable (maintains internal state)
    binary_counter_merkle(binary_counter_merkle const&) = delete;
    binary_counter_merkle& operator=(binary_counter_merkle const&) = delete;
    binary_counter_merkle(binary_counter_merkle&&) = delete;
    binary_counter_merkle& operator=(binary_counter_merkle&&) = delete;

    // Add a value to the counter
    // Precondition: must not be called more than 2^Size - 1 times
    constexpr void add(T x) {
        if (x == zero_) {
            return;
        }

        has_elements_ = true;
        std::size_t i = 0;

        while (i < Size && counter_[i] != zero_) {
            x = op_(counter_[i], x);
            counter_[i] = zero_;
            ++i;
        }

        if (i < Size) {
            counter_[i] = x;
            if (i > last_non_zero_idx_) {
                last_non_zero_idx_ = i;
            }
        }
        // else: overflow, value is lost (precondition violated)
    }

    // Get the final reduced value using merkle-style reduction
    [[nodiscard]]
    constexpr T reduce() {
        if ( ! has_elements_) {
            return zero_;
        }
        return reduce_counter_merkle(
            counter_.begin(),
            counter_.begin() + last_non_zero_idx_,
            op_,
            zero_
        );
    }

    // Reset the counter to empty state
    constexpr void reset() {
        for (std::size_t i = 0; i <= last_non_zero_idx_; ++i) {
            counter_[i] = zero_;
        }
        last_non_zero_idx_ = 0;
        has_elements_ = false;
    }

    [[nodiscard]]
    constexpr std::size_t size() const { return has_elements_ ? last_non_zero_idx_ + 1 : 0; }

    [[nodiscard]]
    constexpr bool empty() const { return !has_elements_; }

private:
    Op op_;
    T zero_;
    std::array<T, Size> counter_;
    std::size_t last_non_zero_idx_;
    bool has_elements_;
};

// =============================================================================
// reduce_with_counter - Convenience function
// =============================================================================

// Reduces a range of values using a binary counter.
// More memory-efficient than std::accumulate for large ranges.
template <std::input_iterator I, std::sentinel_for<I> S,
          BinaryOperation<std::iter_value_t<I>> Op,
          std::size_t Size = 64>
constexpr std::iter_value_t<I> reduce_with_counter(I first, S last, Op op, std::iter_value_t<I> zero) {
    binary_counter<std::iter_value_t<I>, Op, Size> counter(op, zero);

    while (first != last) {
        counter.add(*first);
        ++first;
    }

    return counter.reduce();
}

// Merkle tree version - uses binary_counter_merkle for proper odd duplication
template <std::input_iterator I, std::sentinel_for<I> S,
          BinaryOperation<std::iter_value_t<I>> Op,
          std::size_t Size = 64>
constexpr std::iter_value_t<I> reduce_merkle(I first, S last, Op op, std::iter_value_t<I> zero) {
    binary_counter_merkle<std::iter_value_t<I>, Op, Size> counter(op, zero);

    while (first != last) {
        counter.add(*first);
        ++first;
    }

    return counter.reduce();
}

} // namespace kth::domain::algorithm

#endif // KTH_DOMAIN_ALGORITHM_BINARY_COUNTER_HPP
