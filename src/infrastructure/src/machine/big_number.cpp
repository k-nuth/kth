// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/machine/big_number.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <string>

#include <gmp.h>

#include <boost/multiprecision/cpp_int.hpp>

namespace kth::infrastructure::machine {

// ═══════════════════════════════════════════════════════════════════════════════
// Shared helpers: CScriptNum serialization (little-endian sign-magnitude)
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Serialize a GMP mpz_t to CScriptNum format (little-endian sign-magnitude).
data_chunk mpz_to_scriptnum(mpz_t const& z) {
    if (mpz_sgn(z) == 0) {
        return {};
    }

    // Export absolute value as little-endian bytes.
    size_t count = 0;
    void* raw = mpz_export(nullptr, &count, -1 /*little-endian*/, 1 /*byte*/, -1 /*native endian*/, 0 /*nails*/, z);

    data_chunk result(static_cast<uint8_t*>(raw), static_cast<uint8_t*>(raw) + count);

    // GMP allocates with its own allocator; free with the matching function.
    void (*free_func)(void*, size_t);
    mp_get_memory_functions(nullptr, nullptr, &free_func);
    free_func(raw, count);

    // CScriptNum encoding: if the MSB of the last byte has the sign bit set,
    // we need to add an extra byte for the sign.
    bool const negative = mpz_sgn(z) < 0;
    if (result.back() & 0x80) {
        result.push_back(negative ? 0x80 : 0x00);
    } else if (negative) {
        result.back() |= 0x80;
    }

    return result;
}

// Deserialize CScriptNum format bytes into a GMP mpz_t.
void scriptnum_to_mpz(mpz_t z, data_chunk const& data) {
    if (data.empty()) {
        mpz_set_si(z, 0);
        return;
    }

    // Check sign bit and strip it.
    data_chunk bytes = data;
    bool const negative = (bytes.back() & 0x80) != 0;
    bytes.back() &= 0x7f;

    // Remove trailing zero bytes (they were just sign-extension).
    while (bytes.size() > 1 && bytes.back() == 0) {
        bytes.pop_back();
    }

    // Import little-endian bytes.
    mpz_import(z, bytes.size(), -1 /*little-endian*/, 1 /*byte*/, -1, 0, bytes.data());

    if (negative) {
        mpz_neg(z, z);
    }
}

// Parse a hex string (big-endian, optional leading '-') into mpz_t.
void hex_to_mpz(mpz_t z, std::string_view hex_str) {
    if (hex_str.empty() || hex_str == "0") {
        mpz_set_si(z, 0);
        return;
    }

    bool negative = false;
    if (hex_str.front() == '-') {
        negative = true;
        hex_str.remove_prefix(1);
    }

    // GMP can parse hex strings directly with base 16.
    std::string hex_cstr(hex_str);
    mpz_set_str(z, hex_cstr.c_str(), 16);

    if (negative) {
        mpz_neg(z, z);
    }
}

// Convert mpz_t to hex string (big-endian, with leading '-' for negative).
std::string mpz_to_hex(mpz_t const& z) {
    if (mpz_sgn(z) == 0) return "0";
    char* str = mpz_get_str(nullptr, 16, z);
    std::string result(str);
    void (*free_func)(void*, size_t);
    mp_get_memory_functions(nullptr, nullptr, &free_func);
    free_func(str, std::strlen(str) + 1);
    return result;
}

} // anonymous namespace


// ═══════════════════════════════════════════════════════════════════════════════
// big_number_gmp implementation
// ═══════════════════════════════════════════════════════════════════════════════

struct big_number_gmp::impl {
    mpz_t value;

    impl() { mpz_init(value); }
    ~impl() { mpz_clear(value); }

    impl(impl const& other) {
        mpz_init(value);
        mpz_set(value, other.value);
    }

    impl(impl&& other) noexcept {
        mpz_init(value);
        mpz_swap(value, other.value);
    }

    impl& operator=(impl const& other) {
        if (this != &other) {
            mpz_set(value, other.value);
        }
        return *this;
    }

    impl& operator=(impl&& other) noexcept {
        if (this != &other) {
            mpz_swap(value, other.value);
        }
        return *this;
    }
};

void big_number_gmp::ensure_init() {
    if (p_ == nullptr) {
        p_ = new impl();
    }
}

void big_number_gmp::destroy() {
    delete p_;
    p_ = nullptr;
}

big_number_gmp::big_number_gmp()
    : p_(new impl())
{}

big_number_gmp::~big_number_gmp() {
    destroy();
}

big_number_gmp::big_number_gmp(big_number_gmp const& other)
    : p_(other.p_ ? new impl(*other.p_) : new impl())
{}

big_number_gmp::big_number_gmp(big_number_gmp&& other) noexcept
    : p_(other.p_)
{
    other.p_ = nullptr;
}

big_number_gmp& big_number_gmp::operator=(big_number_gmp const& other) {
    if (this != &other) {
        ensure_init();
        if (other.p_) {
            *p_ = *other.p_;
        } else {
            mpz_set_si(p_->value, 0);
        }
    }
    return *this;
}

big_number_gmp& big_number_gmp::operator=(big_number_gmp&& other) noexcept {
    if (this != &other) {
        destroy();
        p_ = other.p_;
        other.p_ = nullptr;
    }
    return *this;
}

big_number_gmp::big_number_gmp(impl* p)
    : p_(p)
{}

big_number_gmp::big_number_gmp(int64_t value)
    : p_(new impl())
{
    mpz_set_si(p_->value, value);
}

big_number_gmp::big_number_gmp(std::string_view decimal_str)
    : p_(new impl())
{
    if ( ! decimal_str.empty()) {
        std::string s(decimal_str);
        mpz_set_str(p_->value, s.c_str(), 10);
    }
}

big_number_gmp big_number_gmp::from_hex(std::string_view hex_str) {
    auto* p = new impl();
    hex_to_mpz(p->value, hex_str);
    return big_number_gmp(p);
}

data_chunk big_number_gmp::serialize() const {
    if ( ! p_) return {};
    return mpz_to_scriptnum(p_->value);
}

bool big_number_gmp::deserialize(data_chunk const& data) {
    ensure_init();
    scriptnum_to_mpz(p_->value, data);
    return true;
}

std::string big_number_gmp::to_string() const {
    if ( ! p_) return "0";
    char* str = mpz_get_str(nullptr, 10, p_->value);
    std::string result(str);
    void (*free_func)(void*, size_t);
    mp_get_memory_functions(nullptr, nullptr, &free_func);
    free_func(str, std::strlen(str) + 1);
    return result;
}

std::string big_number_gmp::to_hex() const {
    if ( ! p_) return "0";
    return mpz_to_hex(p_->value);
}

int big_number_gmp::sign() const {
    if ( ! p_) return 0;
    return mpz_sgn(p_->value);
}

bool big_number_gmp::is_zero() const { return sign() == 0; }
bool big_number_gmp::is_nonzero() const { return sign() != 0; }
bool big_number_gmp::is_negative() const { return sign() < 0; }

std::optional<int64_t> big_number_gmp::to_int64() const {
    if ( ! p_) return 0;
    if (mpz_fits_slong_p(p_->value)) {
        return static_cast<int64_t>(mpz_get_si(p_->value));
    }
    return std::nullopt;
}

int32_t big_number_gmp::to_int32_saturating() const {
    auto const v = to_int64();
    if (v) {
        return static_cast<int32_t>(std::clamp(*v,
            static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
            static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
    }
    // Value doesn't fit in int64 — saturate based on sign.
    return is_negative() ? std::numeric_limits<int32_t>::min()
                         : std::numeric_limits<int32_t>::max();
}

size_t big_number_gmp::byte_count() const {
    if ( ! p_ || mpz_sgn(p_->value) == 0) return 0;
    return (mpz_sizeinbase(p_->value, 2) + 7) / 8;
}

int big_number_gmp::compare(big_number_gmp const& other) const {
    mpz_t const& a = p_ ? p_->value : *[] { static mpz_t z; static bool inited = false; if (!inited) { mpz_init(z); inited = true; } return &z; }();
    mpz_t const& b = other.p_ ? other.p_->value : *[] { static mpz_t z; static bool inited = false; if (!inited) { mpz_init(z); inited = true; } return &z; }();
    return mpz_cmp(a, b);
}

std::strong_ordering big_number_gmp::operator<=>(big_number_gmp const& other) const {
    int const c = compare(other);
    if (c < 0) return std::strong_ordering::less;
    if (c > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool big_number_gmp::operator==(big_number_gmp const& other) const {
    return compare(other) == 0;
}

// ── Arithmetic helpers ──

#define BN_GMP_BINARY_OP(op_name, mpz_func)                        \
big_number_gmp big_number_gmp::operator op_name(big_number_gmp const& other) const { \
    auto* r = new impl();                                           \
    mpz_func(r->value, p_->value, other.p_->value);                \
    return big_number_gmp(r);                                       \
}

#define BN_GMP_COMPOUND_OP(op_name, mpz_func)                      \
big_number_gmp& big_number_gmp::operator op_name(big_number_gmp const& other) { \
    ensure_init();                                                  \
    mpz_func(p_->value, p_->value, other.p_->value);               \
    return *this;                                                   \
}

BN_GMP_BINARY_OP(+, mpz_add)
BN_GMP_BINARY_OP(-, mpz_sub)
BN_GMP_BINARY_OP(*, mpz_mul)

big_number_gmp big_number_gmp::operator/(big_number_gmp const& other) const {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    auto* r = new impl();
    mpz_tdiv_q(r->value, p_->value, other.p_->value);
    return big_number_gmp(r);
}

big_number_gmp big_number_gmp::operator%(big_number_gmp const& other) const {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    auto* r = new impl();
    mpz_tdiv_r(r->value, p_->value, other.p_->value);
    return big_number_gmp(r);
}

big_number_gmp big_number_gmp::operator-() const {
    auto* r = new impl();
    mpz_neg(r->value, p_->value);
    return big_number_gmp(r);
}

BN_GMP_COMPOUND_OP(+=, mpz_add)
BN_GMP_COMPOUND_OP(-=, mpz_sub)
BN_GMP_COMPOUND_OP(*=, mpz_mul)

big_number_gmp& big_number_gmp::operator/=(big_number_gmp const& other) {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    ensure_init();
    mpz_tdiv_q(p_->value, p_->value, other.p_->value);
    return *this;
}

big_number_gmp& big_number_gmp::operator%=(big_number_gmp const& other) {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    ensure_init();
    mpz_tdiv_r(p_->value, p_->value, other.p_->value);
    return *this;
}

big_number_gmp& big_number_gmp::operator++() {
    ensure_init();
    mpz_add_ui(p_->value, p_->value, 1);
    return *this;
}

big_number_gmp big_number_gmp::operator++(int) {
    big_number_gmp prev(*this);
    ++(*this);
    return prev;
}

big_number_gmp& big_number_gmp::operator--() {
    ensure_init();
    mpz_sub_ui(p_->value, p_->value, 1);
    return *this;
}

big_number_gmp big_number_gmp::operator--(int) {
    big_number_gmp prev(*this);
    --(*this);
    return prev;
}

// ── Bitwise ──

BN_GMP_BINARY_OP(&, mpz_and)
BN_GMP_BINARY_OP(|, mpz_ior)
BN_GMP_BINARY_OP(^, mpz_xor)

big_number_gmp big_number_gmp::operator<<(int shift) const {
    auto* r = new impl();
    mpz_mul_2exp(r->value, p_->value, static_cast<mp_bitcnt_t>(shift));
    return big_number_gmp(r);
}

big_number_gmp big_number_gmp::operator>>(int shift) const {
    auto* r = new impl();
    mpz_fdiv_q_2exp(r->value, p_->value, static_cast<mp_bitcnt_t>(shift));
    return big_number_gmp(r);
}

BN_GMP_COMPOUND_OP(&=, mpz_and)
BN_GMP_COMPOUND_OP(|=, mpz_ior)
BN_GMP_COMPOUND_OP(^=, mpz_xor)

big_number_gmp& big_number_gmp::operator<<=(int shift) {
    ensure_init();
    mpz_mul_2exp(p_->value, p_->value, static_cast<mp_bitcnt_t>(shift));
    return *this;
}

big_number_gmp& big_number_gmp::operator>>=(int shift) {
    ensure_init();
    mpz_fdiv_q_2exp(p_->value, p_->value, static_cast<mp_bitcnt_t>(shift));
    return *this;
}

#undef BN_GMP_BINARY_OP
#undef BN_GMP_COMPOUND_OP

// ── Advanced ──

big_number_gmp big_number_gmp::abs() const {
    auto* r = new impl();
    mpz_abs(r->value, p_->value);
    return big_number_gmp(r);
}

void big_number_gmp::negate() {
    ensure_init();
    mpz_neg(p_->value, p_->value);
}

big_number_gmp big_number_gmp::pow(big_number_gmp const& exp) const {
    auto e = exp.to_int64();
    if ( ! e || *e < 0) throw std::invalid_argument("exponent must be a non-negative integer fitting in int64");
    auto* r = new impl();
    mpz_pow_ui(r->value, p_->value, static_cast<unsigned long>(*e));
    return big_number_gmp(r);
}

big_number_gmp big_number_gmp::pow_mod(big_number_gmp const& exp, big_number_gmp const& mod) const {
    auto* r = new impl();
    mpz_powm(r->value, p_->value, exp.p_->value, mod.p_->value);
    return big_number_gmp(r);
}

big_number_gmp big_number_gmp::math_modulo(big_number_gmp const& mod) const {
    auto* r = new impl();
    mpz_mod(r->value, p_->value, mod.p_->value);
    return big_number_gmp(r);
}

bool big_number_gmp::is_minimally_encoded(data_chunk const& data, size_t max_size) {
    if (data.size() > max_size) return false;
    return data.empty() ||
        (data.back() & 0x7f) != 0 ||
        (data.size() > 1 && (data[data.size() - 2] & 0x80) != 0);
}


// ═══════════════════════════════════════════════════════════════════════════════
// big_number_boost implementation
// ═══════════════════════════════════════════════════════════════════════════════

using boost_int = boost::multiprecision::cpp_int;

struct big_number_boost::impl {
    boost_int value;
};

void big_number_boost::ensure_init() {
    if (p_ == nullptr) {
        p_ = new impl();
    }
}

void big_number_boost::destroy() {
    delete p_;
    p_ = nullptr;
}

big_number_boost::big_number_boost()
    : p_(new impl())
{}

big_number_boost::~big_number_boost() {
    destroy();
}

big_number_boost::big_number_boost(big_number_boost const& other)
    : p_(other.p_ ? new impl{other.p_->value} : new impl())
{}

big_number_boost::big_number_boost(big_number_boost&& other) noexcept
    : p_(other.p_)
{
    other.p_ = nullptr;
}

big_number_boost& big_number_boost::operator=(big_number_boost const& other) {
    if (this != &other) {
        ensure_init();
        p_->value = other.p_ ? other.p_->value : boost_int(0);
    }
    return *this;
}

big_number_boost& big_number_boost::operator=(big_number_boost&& other) noexcept {
    if (this != &other) {
        destroy();
        p_ = other.p_;
        other.p_ = nullptr;
    }
    return *this;
}

big_number_boost::big_number_boost(impl* p)
    : p_(p)
{}

big_number_boost::big_number_boost(int64_t value)
    : p_(new impl{boost_int(value)})
{}

big_number_boost::big_number_boost(std::string_view decimal_str)
    : p_(new impl())
{
    if ( ! decimal_str.empty()) {
        p_->value = boost_int(std::string(decimal_str));
    }
}

big_number_boost big_number_boost::from_hex(std::string_view hex_str) {
    auto* p = new impl();
    if ( ! hex_str.empty() && hex_str != "0") {
        bool negative = false;
        if (hex_str.front() == '-') {
            negative = true;
            hex_str.remove_prefix(1);
        }
        p->value = boost_int("0x" + std::string(hex_str));
        if (negative) {
            p->value = -p->value;
        }
    }
    return big_number_boost(p);
}

// CScriptNum serialization using boost_int.
data_chunk big_number_boost::serialize() const {
    if ( ! p_ || p_->value == 0) return {};

    boost_int abs_val = boost::multiprecision::abs(p_->value);
    bool negative = p_->value < 0;

    // Extract bytes in little-endian order.
    data_chunk result;
    boost_int tmp = abs_val;
    while (tmp > 0) {
        result.push_back(static_cast<uint8_t>(tmp & 0xff));
        tmp >>= 8;
    }

    if (result.empty()) return {};

    // Handle sign bit.
    if (result.back() & 0x80) {
        result.push_back(negative ? 0x80 : 0x00);
    } else if (negative) {
        result.back() |= 0x80;
    }

    return result;
}

bool big_number_boost::deserialize(data_chunk const& data) {
    ensure_init();
    if (data.empty()) {
        p_->value = 0;
        return true;
    }

    data_chunk bytes = data;
    bool const negative = (bytes.back() & 0x80) != 0;
    bytes.back() &= 0x7f;

    // Build value from little-endian bytes.
    p_->value = 0;
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it) {
        p_->value <<= 8;
        p_->value |= *it;
    }

    if (negative) {
        p_->value = -p_->value;
    }
    return true;
}

std::string big_number_boost::to_string() const {
    if ( ! p_) return "0";
    return p_->value.str();
}

std::string big_number_boost::to_hex() const {
    if ( ! p_ || p_->value == 0) return "0";
    // Boost hex output includes "0x" prefix, strip it.
    std::string hex = p_->value.str(0, std::ios_base::hex);
    return hex;
}

int big_number_boost::sign() const {
    if ( ! p_) return 0;
    return p_->value.sign();
}

bool big_number_boost::is_zero() const { return sign() == 0; }
bool big_number_boost::is_nonzero() const { return sign() != 0; }
bool big_number_boost::is_negative() const { return sign() < 0; }

std::optional<int64_t> big_number_boost::to_int64() const {
    if ( ! p_) return 0;
    static const boost_int min_val(std::numeric_limits<int64_t>::min());
    static const boost_int max_val(std::numeric_limits<int64_t>::max());
    if (p_->value >= min_val && p_->value <= max_val) {
        return static_cast<int64_t>(p_->value);
    }
    return std::nullopt;
}

int32_t big_number_boost::to_int32_saturating() const {
    auto const v = to_int64();
    if (v) {
        return static_cast<int32_t>(std::clamp(*v,
            static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
            static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
    }
    return is_negative() ? std::numeric_limits<int32_t>::min()
                         : std::numeric_limits<int32_t>::max();
}

size_t big_number_boost::byte_count() const {
    if ( ! p_ || p_->value == 0) return 0;
    auto bits = boost::multiprecision::msb(boost::multiprecision::abs(p_->value)) + 1;
    return (bits + 7) / 8;
}

int big_number_boost::compare(big_number_boost const& other) const {
    boost_int const& a = p_ ? p_->value : boost_int(0);
    boost_int const& b = other.p_ ? other.p_->value : boost_int(0);
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

std::strong_ordering big_number_boost::operator<=>(big_number_boost const& other) const {
    int const c = compare(other);
    if (c < 0) return std::strong_ordering::less;
    if (c > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool big_number_boost::operator==(big_number_boost const& other) const {
    return compare(other) == 0;
}

// ── Arithmetic ──

#define BN_BOOST_BINARY_OP(op_sym)                                  \
big_number_boost big_number_boost::operator op_sym(big_number_boost const& other) const { \
    return big_number_boost(new impl{p_->value op_sym other.p_->value}); \
}

#define BN_BOOST_COMPOUND_OP(op_sym)                                \
big_number_boost& big_number_boost::operator op_sym##=(big_number_boost const& other) { \
    ensure_init();                                                  \
    p_->value op_sym##= other.p_->value;                            \
    return *this;                                                   \
}

BN_BOOST_BINARY_OP(+)
BN_BOOST_BINARY_OP(-)
BN_BOOST_BINARY_OP(*)

big_number_boost big_number_boost::operator/(big_number_boost const& other) const {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    return big_number_boost(new impl{p_->value / other.p_->value});
}

big_number_boost big_number_boost::operator%(big_number_boost const& other) const {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    return big_number_boost(new impl{p_->value % other.p_->value});
}

big_number_boost big_number_boost::operator-() const {
    return big_number_boost(new impl{-(p_->value)});
}

BN_BOOST_COMPOUND_OP(+)
BN_BOOST_COMPOUND_OP(-)
BN_BOOST_COMPOUND_OP(*)

big_number_boost& big_number_boost::operator/=(big_number_boost const& other) {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    ensure_init();
    p_->value /= other.p_->value;
    return *this;
}

big_number_boost& big_number_boost::operator%=(big_number_boost const& other) {
    if (other.is_zero()) throw std::invalid_argument("division by zero");
    ensure_init();
    p_->value %= other.p_->value;
    return *this;
}

big_number_boost& big_number_boost::operator++() {
    ensure_init();
    ++p_->value;
    return *this;
}

big_number_boost big_number_boost::operator++(int) {
    big_number_boost prev(*this);
    ++(*this);
    return prev;
}

big_number_boost& big_number_boost::operator--() {
    ensure_init();
    --p_->value;
    return *this;
}

big_number_boost big_number_boost::operator--(int) {
    big_number_boost prev(*this);
    --(*this);
    return prev;
}

// ── Bitwise ──

BN_BOOST_BINARY_OP(&)
BN_BOOST_BINARY_OP(|)
BN_BOOST_BINARY_OP(^)

big_number_boost big_number_boost::operator<<(int shift) const {
    return big_number_boost(new impl{p_->value << shift});
}

big_number_boost big_number_boost::operator>>(int shift) const {
    return big_number_boost(new impl{p_->value >> shift});
}

BN_BOOST_COMPOUND_OP(&)
BN_BOOST_COMPOUND_OP(|)
BN_BOOST_COMPOUND_OP(^)

big_number_boost& big_number_boost::operator<<=(int shift) {
    ensure_init();
    p_->value <<= shift;
    return *this;
}

big_number_boost& big_number_boost::operator>>=(int shift) {
    ensure_init();
    p_->value >>= shift;
    return *this;
}

#undef BN_BOOST_BINARY_OP
#undef BN_BOOST_COMPOUND_OP

// ── Advanced ──

big_number_boost big_number_boost::abs() const {
    return big_number_boost(new impl{boost::multiprecision::abs(p_->value)});
}

void big_number_boost::negate() {
    ensure_init();
    p_->value = -p_->value;
}

big_number_boost big_number_boost::pow(big_number_boost const& exp) const {
    auto e = exp.to_int64();
    if ( ! e || *e < 0) throw std::invalid_argument("exponent must be a non-negative integer fitting in int64");
    return big_number_boost(new impl{boost::multiprecision::pow(p_->value, static_cast<unsigned>(*e))});
}

big_number_boost big_number_boost::pow_mod(big_number_boost const& exp, big_number_boost const& mod) const {
    return big_number_boost(new impl{boost::multiprecision::powm(p_->value, exp.p_->value, mod.p_->value)});
}

bool big_number_boost::is_minimally_encoded(data_chunk const& data, size_t max_size) {
    if (data.size() > max_size) return false;
    return data.empty() ||
        (data.back() & 0x7f) != 0 ||
        (data.size() > 1 && (data[data.size() - 2] & 0x80) != 0);
}

big_number_boost big_number_boost::math_modulo(big_number_boost const& mod) const {
    // Mathematical modulo: result is always in [0, mod).
    boost_int r = p_->value % mod.p_->value;
    if (r < 0) r += boost::multiprecision::abs(mod.p_->value);
    return big_number_boost(new impl{std::move(r)});
}

} // namespace kth::infrastructure::machine
