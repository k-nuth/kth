// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_MACHINE_BIG_NUMBER_HPP_
#define KTH_INFRASTRUCTURE_MACHINE_BIG_NUMBER_HPP_

#include <cstdint>
#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kth/infrastructure/utility/data.hpp>

namespace kth::infrastructure::machine {

// ─── big_number_gmp — libgmp backend ────────────────────────────────────────

class big_number_gmp {
public:
    big_number_gmp();
    ~big_number_gmp();

    big_number_gmp(big_number_gmp const& other);
    big_number_gmp(big_number_gmp&& other) noexcept;
    big_number_gmp& operator=(big_number_gmp const& other);
    big_number_gmp& operator=(big_number_gmp&& other) noexcept;

    explicit big_number_gmp(int64_t value);
    explicit big_number_gmp(std::string_view decimal_str);

    static big_number_gmp from_hex(std::string_view hex_str);

    // ── Serialization (CScriptNum format: little-endian sign-magnitude) ──
    [[nodiscard]] data_chunk serialize() const;
    bool deserialize(data_chunk const& data);

    // ── String conversions ──
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_hex() const;

    // ── Properties ──
    [[nodiscard]] int sign() const;                      // -1, 0, or 1
    [[nodiscard]] bool is_zero() const;
    [[nodiscard]] bool is_nonzero() const;
    [[nodiscard]] bool is_negative() const;
    [[nodiscard]] std::optional<int64_t> to_int64() const;
    [[nodiscard]] int32_t to_int32_saturating() const;
    [[nodiscard]] size_t byte_count() const;

    // ── Comparison ──
    [[nodiscard]] int compare(big_number_gmp const& other) const;
    [[nodiscard]] std::strong_ordering operator<=>(big_number_gmp const& other) const;
    [[nodiscard]] bool operator==(big_number_gmp const& other) const;

    // ── Arithmetic ──
    big_number_gmp operator+(big_number_gmp const& other) const;
    big_number_gmp operator-(big_number_gmp const& other) const;
    big_number_gmp operator*(big_number_gmp const& other) const;
    big_number_gmp operator/(big_number_gmp const& other) const;
    big_number_gmp operator%(big_number_gmp const& other) const;
    big_number_gmp operator-() const;

    big_number_gmp& operator+=(big_number_gmp const& other);
    big_number_gmp& operator-=(big_number_gmp const& other);
    big_number_gmp& operator*=(big_number_gmp const& other);
    big_number_gmp& operator/=(big_number_gmp const& other);
    big_number_gmp& operator%=(big_number_gmp const& other);

    big_number_gmp& operator++();
    big_number_gmp operator++(int);
    big_number_gmp& operator--();
    big_number_gmp operator--(int);

    // ── Bitwise ──
    big_number_gmp operator&(big_number_gmp const& other) const;
    big_number_gmp operator|(big_number_gmp const& other) const;
    big_number_gmp operator^(big_number_gmp const& other) const;
    big_number_gmp operator<<(int shift) const;
    big_number_gmp operator>>(int shift) const;

    big_number_gmp& operator&=(big_number_gmp const& other);
    big_number_gmp& operator|=(big_number_gmp const& other);
    big_number_gmp& operator^=(big_number_gmp const& other);
    big_number_gmp& operator<<=(int shift);
    big_number_gmp& operator>>=(int shift);

    // ── Advanced ──
    [[nodiscard]] big_number_gmp abs() const;
    void negate();
    [[nodiscard]] big_number_gmp pow(big_number_gmp const& exp) const;
    [[nodiscard]] big_number_gmp pow_mod(big_number_gmp const& exp, big_number_gmp const& mod) const;
    [[nodiscard]] big_number_gmp math_modulo(big_number_gmp const& mod) const;

    // ── Compatibility with number interface (for interpreter integration) ──
    [[nodiscard]] data_chunk data() const { return serialize(); }
    data_chunk data() { return serialize(); }
    bool set_data(data_chunk const& d, size_t max_size) {
        if (d.size() > max_size) return false;
        return deserialize(d);
    }
    [[nodiscard]] bool is_true() const { return is_nonzero(); }
    [[nodiscard]] bool is_false() const { return is_zero(); }

    // comparison with int (for `== 0`, `< 0`, etc.)
    // TODO(optimize): avoid temporary big_number_gmp construction — use mpz_cmp_si directly
    [[nodiscard]] bool operator==(int64_t other) const { return *this == big_number_gmp(other); }
    [[nodiscard]] std::strong_ordering operator<=>(int64_t other) const { return *this <=> big_number_gmp(other); }

    [[nodiscard]] static bool is_minimally_encoded(data_chunk const& data, size_t max_size);

private:
    struct impl;
    impl* p_ = nullptr;

    explicit big_number_gmp(impl* p);
    void ensure_init();
    void destroy();
};


// ─── big_number_boost — Boost.Multiprecision backend ────────────────────────

class big_number_boost {
public:
    big_number_boost();
    ~big_number_boost();

    big_number_boost(big_number_boost const& other);
    big_number_boost(big_number_boost&& other) noexcept;
    big_number_boost& operator=(big_number_boost const& other);
    big_number_boost& operator=(big_number_boost&& other) noexcept;

    explicit big_number_boost(int64_t value);
    explicit big_number_boost(std::string_view decimal_str);

    static big_number_boost from_hex(std::string_view hex_str);

    [[nodiscard]] data_chunk serialize() const;
    bool deserialize(data_chunk const& data);

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string to_hex() const;

    [[nodiscard]] int sign() const;
    [[nodiscard]] bool is_zero() const;
    [[nodiscard]] bool is_nonzero() const;
    [[nodiscard]] bool is_negative() const;
    [[nodiscard]] std::optional<int64_t> to_int64() const;
    [[nodiscard]] int32_t to_int32_saturating() const;
    [[nodiscard]] size_t byte_count() const;

    [[nodiscard]] int compare(big_number_boost const& other) const;
    [[nodiscard]] std::strong_ordering operator<=>(big_number_boost const& other) const;
    [[nodiscard]] bool operator==(big_number_boost const& other) const;

    big_number_boost operator+(big_number_boost const& other) const;
    big_number_boost operator-(big_number_boost const& other) const;
    big_number_boost operator*(big_number_boost const& other) const;
    big_number_boost operator/(big_number_boost const& other) const;
    big_number_boost operator%(big_number_boost const& other) const;
    big_number_boost operator-() const;

    big_number_boost& operator+=(big_number_boost const& other);
    big_number_boost& operator-=(big_number_boost const& other);
    big_number_boost& operator*=(big_number_boost const& other);
    big_number_boost& operator/=(big_number_boost const& other);
    big_number_boost& operator%=(big_number_boost const& other);

    big_number_boost& operator++();
    big_number_boost operator++(int);
    big_number_boost& operator--();
    big_number_boost operator--(int);

    big_number_boost operator&(big_number_boost const& other) const;
    big_number_boost operator|(big_number_boost const& other) const;
    big_number_boost operator^(big_number_boost const& other) const;
    big_number_boost operator<<(int shift) const;
    big_number_boost operator>>(int shift) const;

    big_number_boost& operator&=(big_number_boost const& other);
    big_number_boost& operator|=(big_number_boost const& other);
    big_number_boost& operator^=(big_number_boost const& other);
    big_number_boost& operator<<=(int shift);
    big_number_boost& operator>>=(int shift);

    [[nodiscard]] big_number_boost abs() const;
    void negate();
    [[nodiscard]] big_number_boost pow(big_number_boost const& exp) const;
    [[nodiscard]] big_number_boost pow_mod(big_number_boost const& exp, big_number_boost const& mod) const;
    [[nodiscard]] big_number_boost math_modulo(big_number_boost const& mod) const;

    // ── Compatibility with number interface (for interpreter integration) ──
    [[nodiscard]] data_chunk data() const { return serialize(); }
    data_chunk data() { return serialize(); }
    bool set_data(data_chunk const& d, size_t max_size) {
        if (d.size() > max_size) return false;
        return deserialize(d);
    }
    [[nodiscard]] bool is_true() const { return is_nonzero(); }
    [[nodiscard]] bool is_false() const { return is_zero(); }

    // TODO(optimize): avoid temporary big_number_boost construction — compare directly with cpp_int(other)
    [[nodiscard]] bool operator==(int64_t other) const { return *this == big_number_boost(other); }
    [[nodiscard]] std::strong_ordering operator<=>(int64_t other) const { return *this <=> big_number_boost(other); }

    [[nodiscard]] static bool is_minimally_encoded(data_chunk const& data, size_t max_size);

private:
    struct impl;
    impl* p_ = nullptr;

    explicit big_number_boost(impl* p);
    void ensure_init();
    void destroy();
};


// ─── Default backend selection ──────────────────────────────────────────────

#if defined(KTH_BIGNUM_BACKEND_BOOST)
using big_number = big_number_boost;
#else
using big_number = big_number_gmp;
#endif

} // namespace kth::infrastructure::machine


// ─── Concept: script_number ─────────────────────────────────────────────────

namespace kth::infrastructure::machine::concepts {

template <typename T>
concept script_number = requires(T a, T b, T const ca, T const cb, int64_t i, int shift, data_chunk const& dc, size_t sz) {
    // Construction
    { T() };
    { T(i) };
    { T(std::string_view{}) };

    // Serialization (native)
    { ca.serialize() } -> std::same_as<data_chunk>;
    { a.deserialize(dc) } -> std::same_as<bool>;

    // Serialization (interpreter compatibility)
    { ca.data() } -> std::same_as<data_chunk>;
    { a.set_data(dc, sz) } -> std::same_as<bool>;

    // String conversions
    { ca.to_string() } -> std::same_as<std::string>;

    // Properties
    { ca.sign() } -> std::same_as<int>;
    { ca.is_zero() } -> std::same_as<bool>;
    { ca.is_nonzero() } -> std::same_as<bool>;
    { ca.is_negative() } -> std::same_as<bool>;
    { ca.is_true() } -> std::same_as<bool>;
    { ca.is_false() } -> std::same_as<bool>;
    { ca.to_int64() } -> std::same_as<std::optional<int64_t>>;
    { ca.to_int32_saturating() } -> std::same_as<int32_t>;
    { ca.byte_count() } -> std::same_as<size_t>;

    // Comparison (with same type)
    { ca.compare(cb) } -> std::same_as<int>;
    { ca == cb } -> std::same_as<bool>;
    { ca <=> cb } -> std::same_as<std::strong_ordering>;

    // Comparison (with int64_t)
    { ca == i } -> std::same_as<bool>;

    // Arithmetic (binary)
    { ca + cb } -> std::same_as<T>;
    { ca - cb } -> std::same_as<T>;
    { ca * cb } -> std::same_as<T>;
    { ca / cb } -> std::same_as<T>;
    { ca % cb } -> std::same_as<T>;
    { -ca } -> std::same_as<T>;

    // Compound assignment
    { a += b } -> std::same_as<T&>;
    { a -= b } -> std::same_as<T&>;
    { a *= b } -> std::same_as<T&>;
    { a /= b } -> std::same_as<T&>;
    { a %= b } -> std::same_as<T&>;

    // Increment/decrement
    { ++a } -> std::same_as<T&>;
    { a++ } -> std::same_as<T>;
    { --a } -> std::same_as<T&>;
    { a-- } -> std::same_as<T>;

    // Bitwise
    { ca & cb } -> std::same_as<T>;
    { ca | cb } -> std::same_as<T>;
    { ca ^ cb } -> std::same_as<T>;
    { ca << shift } -> std::same_as<T>;
    { ca >> shift } -> std::same_as<T>;

    // Advanced
    { ca.abs() } -> std::same_as<T>;
    { a.negate() } -> std::same_as<void>;

    // Minimal encoding check
    { T::is_minimally_encoded(dc, sz) } -> std::same_as<bool>;
};

// Static verification that both backends satisfy the concept.
static_assert(script_number<big_number_gmp>);
static_assert(script_number<big_number_boost>);

} // namespace kth::infrastructure::machine::concepts


#endif // KTH_INFRASTRUCTURE_MACHINE_BIG_NUMBER_HPP_
