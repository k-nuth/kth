// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/concepts.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/machine/opcode.hpp>

#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/serializer.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>

namespace kth::domain::chain {

namespace encoding {
inline constexpr uint8_t PREFIX_BYTE = uint8_t(::kth::domain::machine::opcode::special_token_prefix);
} // namespace encoding

using token_id_t = hash_digest;

// CashTokens encode token amounts as VLQ integers in the range
// [0, 2^63 − 1]. `amount_t` uses `uint64_t` to match the other kth
// amount types (output::value, utxo::amount) and to remove the silent
// narrowing from `read_variable_little_endian()` (which itself
// returns `uint64_t`) into a signed enum. A valid fungible amount is
// strictly positive (see `is_valid(fungible)` below), so callers can
// treat `0` as the implicit "no fungible payload" sentinel — that's
// also what `get_amount` returns in that case so the consensus code
// can keep tallying without per-token branching.
enum class amount_t : uint64_t {};

using commitment_t = data_chunk;

// The values assigned to the low-order nibble of the token bitfield
// byte represent the "permissions" of an NFT. Pure-fungible tokens
// must use `none` (0x0). Only the three specified bitpatterns are
// valid capabilities.
enum class capability_t : uint8_t {
    none    = 0x00,  // No capability: either fungible-only or immutable NFT.
    mut     = 0x01,  // Mutable NFT: the payload can be altered.
    minting = 0x02,  // Minting NFT: used to mint new tokens of the category.
};

// Returns true iff the byte matches one of the three named capability
// literals. Used by the wire parser and by `is_valid(non_fungible)`
// to reject hand-built values whose capability byte is out of range
// (the type itself can hold any uint8_t by casting).
inline constexpr
bool is_valid_capability(uint8_t b) {
    return b <= uint8_t(capability_t::minting);
}

inline constexpr
bool is_valid_capability(capability_t c) {
    return is_valid_capability(uint8_t(c));
}

struct fungible {
    amount_t amount;

    friend constexpr
    auto operator<=>(fungible const&, fungible const&) = default;
};

struct non_fungible {
    capability_t capability;
    commitment_t commitment;

    friend
    auto operator<=>(non_fungible const&, non_fungible const&) = default;
};

// A token payload that carries both a fungible amount and an NFT for
// the same category. Previously modeled as `std::pair<fungible,
// non_fungible>` with anonymous `.first` / `.second` accessors; the
// named struct keeps the accessor names aligned with the concepts
// each field represents. Note: the CashTokens wire format serializes
// the NFT portion before the fungible portion — see the encoding
// helpers below.
struct both_kinds {
    fungible fungible_part;
    non_fungible non_fungible_part;

    friend
    auto operator<=>(both_kinds const&, both_kinds const&) = default;
};

struct token_data_t {
    token_id_t id;
    std::variant<fungible, non_fungible, both_kinds> data;

    // Boolean conversion keeps the C-API factories honest: the binding
    // generator routes every factory through `kth::check_valid` which
    // gates the heap allocation on this conversion. Without it the
    // "returns NULL on invalid input" contract advertised in the
    // generated docstrings would be unreachable. Definition is after
    // the free-function `is_valid` overload below.
    explicit operator bool() const;

    // `std::variant` gets a defaulted `<=>` in C++20 when its
    // alternatives all have one, which is the case here. The C++
    // language synthesises `==` from a defaulted `<=>` automatically.
    friend
    auto operator<=>(token_data_t const&, token_data_t const&) = default;
};

using token_data_opt = std::optional<token_data_t>;

// Factories.
// ---------------------------------------------------------------------------
// Free-function constructors for each variant arm. The generator
// picks them up via the extra_namespaces scan and emits matching
// `kth_chain_token_data_make_*` entry points in the C API. Taking
// `uint64_t` instead of `amount_t` in the C++ signature keeps the
// generated C bindings working with plain integral types without
// the binding layer needing to know about the strong enum.

inline
token_data_t make_fungible(token_id_t id, uint64_t amount) {
    return token_data_t{id, fungible{amount_t{amount}}};
}

inline
token_data_t make_non_fungible(token_id_t id, capability_t capability, commitment_t commitment) {
    return token_data_t{id, non_fungible{capability, std::move(commitment)}};
}

inline
token_data_t make_both(token_id_t id, uint64_t amount, capability_t capability, commitment_t commitment) {
    return token_data_t{
        id,
        both_kinds{
            fungible{amount_t{amount}},
            non_fungible{capability, std::move(commitment)}
        }
    };
}

// Discriminator for the variant. Mirrors the C-API `kth_token_kind_t`
// so both layers can share the same naming when talking about which
// arm of the variant is active.
enum class kind : uint8_t {
    fungible_only = 0,
    non_fungible_only = 1,
    both = 2,
};

inline constexpr
kind get_kind(token_data_t const& td) {
    if (std::holds_alternative<fungible>(td.data)) return kind::fungible_only;
    if (std::holds_alternative<non_fungible>(td.data)) return kind::non_fungible_only;
    return kind::both;
}

// Validity.
// ---------------------------------------------------------------------------

inline constexpr
bool is_valid(fungible const& x) {
    // CashTokens CHIP: amounts are non-zero positive VLQ integers
    // capped at 2^63 − 1. `amount_t`'s underlying is `uint64_t`, so
    // the lower bound excludes 0 (implicit "no amount" sentinel)
    // and the upper bound rejects anything that would narrow to a
    // negative `int64_t` when consensus code converts it back.
    return x.amount > amount_t{0}
        && uint64_t(x.amount) <= uint64_t(INT64_MAX);
}

inline constexpr
bool is_valid(non_fungible const& x) {
    return is_valid_capability(x.capability);
}

inline constexpr
bool is_valid(both_kinds const& x) {
    return is_valid(x.fungible_part) && is_valid(x.non_fungible_part);
}

inline constexpr
bool is_valid(token_data_t const& x) {
    // Note: we deliberately do NOT reject `id == null_hash` here
    // even though a "real" token always has a non-null category.
    // The VM constructs transient `token_data_t` instances with a
    // zeroed id while building intermediate state and expects
    // `is_valid` to depend only on the variant arm being well-formed.
    auto const visitor = [](auto&& arg) { return is_valid(arg); };
    return std::visit(visitor, x.data);
}

inline constexpr
bool is_valid(token_data_opt const& x) {
    return x.has_value() ? is_valid(x.value()) : true;
}

inline
token_data_t::operator bool() const {
    return is_valid(*this);
}

// Accessors.
// ---------------------------------------------------------------------------

// Returns the fungible amount carried by the token, or `0` when the
// token has no fungible payload (i.e. it is a pure NFT). The
// consensus code in `transaction_basis.cpp` calls this on every
// token-bearing input/output and tallies the results by category, so
// returning `0` for the "no fungible" case lets the math work
// without per-token branching. Per the CashTokens spec a valid
// fungible amount is strictly positive, so `0` unambiguously means
// "no fungible payload" and never collides with a legitimate value.
inline
uint64_t get_amount(token_data_t const& td) {
    if (auto const* f = std::get_if<fungible>(&td.data)) return uint64_t(f->amount);
    if (auto const* b = std::get_if<both_kinds>(&td.data)) return uint64_t(b->fungible_part.amount);
    return 0;
}

inline
bool has_nft(token_data_t const& td) {
    return std::holds_alternative<non_fungible>(td.data)
        || std::holds_alternative<both_kinds>(td.data);
}

inline
non_fungible const& get_nft(token_data_t const& td) {
    if (auto const* nf = std::get_if<non_fungible>(&td.data)) return *nf;
    return std::get<both_kinds>(td.data).non_fungible_part;
}

inline
bool is_fungible_only(token_data_t const& td) {
    return std::holds_alternative<fungible>(td.data);
}

// Projection accessors for the NFT part of a token_data_t. They return
// the default (`none` capability / empty commitment) when the token
// has no NFT payload, so a consumer that forgets to check
// `has_nft(td)` first gets a well-defined result instead of a
// precondition abort. These exist primarily so the C-API binding
// layer can expose the NFT fields without reaching into the variant
// through a reference-returning helper.

inline
capability_t get_nft_capability(token_data_t const& td) {
    if ( ! has_nft(td)) return capability_t::none;
    return get_nft(td).capability;
}

inline
commitment_t get_nft_commitment(token_data_t const& td) {
    if ( ! has_nft(td)) return commitment_t{};
    return get_nft(td).commitment;
}

inline
bool is_immutable_nft(token_data_t const& td) {
    return has_nft(td) && get_nft(td).capability == capability_t::none;
}

inline
bool is_mutable_nft(token_data_t const& td) {
    return has_nft(td) && get_nft(td).capability == capability_t::mut;
}

inline
bool is_minting_nft(token_data_t const& td) {
    return has_nft(td) && get_nft(td).capability == capability_t::minting;
}

namespace token::encoding {

// Safety bound applied when parsing commitment data from the wire.
// The CashTokens CHIP doesn't hard-cap commitment length, but script
// element pushes in the surrounding VM are limited to this value, so
// accepting anything larger from an attacker-crafted token prefix
// would just let them force unbounded allocations.
inline constexpr
size_t max_commitment_size = kth::max_push_data_size_legacy;

inline constexpr
size_t serialized_size(fungible const& x) {
    return kth::size_variable_integer(uint64_t(x.amount));
}

inline constexpr
size_t serialized_size(non_fungible const& x) {
    if (std::size(x.commitment) == 0) return 0;
    return kth::size_variable_integer(x.commitment.size()) + x.commitment.size();
}

inline constexpr
size_t serialized_size(both_kinds const& x) {
    return serialized_size(x.fungible_part) + serialized_size(x.non_fungible_part);
}

inline constexpr
size_t serialized_size(token_data_t const& x) {
    auto const visitor = [](auto&& arg) {
        return serialized_size(arg);
    };

    return std::size(x.id) +
           1 + // bitfield byte
           std::visit(visitor, x.data);
}

inline constexpr
size_t serialized_size(token_data_opt const& x) {
    return x.has_value() ? serialized_size(x.value()) : 0;
}

// High-order nibble of the bitfield byte: structure of the payload.
// The nibble may combine `has_amount`, `has_nft`, `has_commitment`
// (via bitwise-OR) but must not be zero and must not set the
// `reserved` bit.
enum class structure_t : uint8_t {
    has_amount     = 0x10,
    has_nft        = 0x20,
    has_commitment = 0x40,
    reserved       = 0x80,
};

inline constexpr
bool has_nft(uint8_t bitfield) {
    return bitfield & uint8_t(structure_t::has_nft);
}

inline constexpr
bool has_amount(uint8_t bitfield) {
    return bitfield & uint8_t(structure_t::has_amount);
}

inline constexpr
bool has_commitment(uint8_t bitfield) {
    return bitfield & uint8_t(structure_t::has_commitment);
}

inline constexpr
std::pair<uint8_t, uint8_t> nibbles(uint8_t bitfield) {
    uint8_t const higher = bitfield & 0xf0u;
    uint8_t const lower  = bitfield & 0x0fu;
    return {higher, lower};
}

inline constexpr
capability_t capability(uint8_t bitfield) {
    auto const [_, c] = nibbles(bitfield);
    return capability_t{c};
}

inline constexpr
uint8_t bitfield(fungible const& /*x*/) {
    return uint8_t(structure_t::has_amount);
}

inline constexpr
uint8_t bitfield(non_fungible const& x) {
    uint8_t const commitment_bit = x.commitment.empty()
        ? uint8_t{0}
        : uint8_t(structure_t::has_commitment);
    return commitment_bit
         | uint8_t(structure_t::has_nft)
         | uint8_t(x.capability);
}

inline constexpr
uint8_t bitfield(both_kinds const& x) {
    return bitfield(x.fungible_part) | bitfield(x.non_fungible_part);
}

inline constexpr
uint8_t bitfield(token_data_t const& x) {
    auto const visitor = [](auto&& arg) {
        return bitfield(arg);
    };
    return std::visit(visitor, x.data);
}

template <typename W>
inline constexpr
void to_data(W& sink, fungible const& x) {
    sink.write_variable_little_endian(uint64_t(x.amount));
}

template <typename W>
inline constexpr
void to_data(W& sink, non_fungible const& x) {
    if (x.commitment.empty()) return;

    sink.write_size_little_endian(x.commitment.size());
    sink.write_bytes(x.commitment);
}

template <typename W>
inline constexpr
void to_data(W& sink, both_kinds const& x) {
    // CashTokens wire format writes the NFT portion first, then the
    // fungible amount — keep that order even with the named fields.
    to_data(sink, x.non_fungible_part);
    to_data(sink, x.fungible_part);
}

template <typename W>
inline constexpr
void to_data(W& sink, token_data_t const& x) {
    sink.write_hash(x.id);
    sink.write_byte(bitfield(x));

    auto const visitor = [&sink](auto&& arg) {
        to_data(sink, arg);
    };
    std::visit(visitor, x.data);
}

inline constexpr
bool is_valid_bitfield(uint8_t bitfield) {
    auto const [structure, capability] = nibbles(bitfield);

    // At least one structure bit must be set, and `reserved` must not.
    if (structure >= 0x80u || structure == 0x00u) return false;

    // Capability nibble outside {0x00, 0x01, 0x02} is invalid.
    if ( ! is_valid_capability(capability)) return false;

    // A prefix that encodes no tokens (neither nft nor amount) is invalid.
    if ( ! has_nft(bitfield) && ! has_amount(bitfield)) return false;

    // A prefix without has_nft must encode a `none` capability.
    if ( ! has_nft(bitfield) && capability != 0u) return false;

    // has_commitment without has_nft is invalid.
    if ( ! has_nft(bitfield) && has_commitment(bitfield)) return false;
    return true;
}

namespace detail {

inline
expect<void> from_data(byte_reader& reader, fungible& x, bool /*has_commitment*/) {
    auto const amt = reader.read_variable_little_endian();
    if ( ! amt) {
        return std::unexpected(amt.error());
    }
    x.amount = amount_t(*amt);
    return {};
}

inline
expect<void> from_data(byte_reader& reader, non_fungible& x, bool has_commitment) {
    if ( ! has_commitment) {
        return {};
    }

    auto const size = reader.read_size_little_endian();
    if ( ! size) {
        return std::unexpected(size.error());
    }
    if (*size > max_commitment_size) {
        return std::unexpected(error::invalid_script_size);
    }
    auto commitment = reader.read_bytes(*size);
    if ( ! commitment) {
        return std::unexpected(commitment.error());
    }
    x.commitment = commitment_t(std::begin(*commitment), std::end(*commitment));
    return {};
}

inline
expect<void> from_data(byte_reader& reader, both_kinds& x, bool has_commitment) {
    auto res = from_data(reader, x.non_fungible_part, has_commitment);
    if ( ! res) {
        return res;
    }
    res = from_data(reader, x.fungible_part, has_commitment);
    if ( ! res) {
        return res;
    }
    return {};
}

} // namespace detail

inline
expect<token_data_t> from_data(byte_reader& reader) {
    auto const id = read_hash(reader);
    if ( ! id) {
        return std::unexpected(id.error());
    }

    auto const bitfield = reader.read_byte();
    if ( ! bitfield) {
        return std::unexpected(bitfield.error());
    }
    if ( ! is_valid_bitfield(*bitfield)) {
        return std::unexpected(error::invalid_bitfield);
    }

    token_data_t x = {
        *id,
        fungible {}
    };
    if (has_nft(*bitfield) && has_amount(*bitfield)) {
        x.data = both_kinds {
            fungible {},
            non_fungible {capability(*bitfield), {}}
        };
    } else if (has_nft(*bitfield)) {
        x.data = non_fungible { .capability = capability(*bitfield) };
    }

    auto const visitor = [&reader, bitfield](auto&& arg) -> expect<void> {
        return detail::from_data(reader, arg, has_commitment(*bitfield));
    };

    auto const res = std::visit(visitor, x.data);
    if ( ! res) {
        return std::unexpected(res.error());
    }

    return x;
}

// Non-templated entry points that adapt the templated `to_data<W>`
// above to an owned `data_chunk`. Historically these lived in
// `token_data_serialization.hpp`; consolidating them here means the
// C-API binding generator — which parses a single header per class —
// can discover the serializer without extra plumbing. The shared
// body is factored into a helper so adding a new payload type only
// needs a one-line forwarder.

namespace detail {

template <typename T>
data_chunk to_data_impl(T const& x) {
    data_chunk data;
    auto const size = serialized_size(x);
    data.reserve(size);
    data_sink ostream(data);
    ostream_writer sink_w(ostream);
    to_data(sink_w, x);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

} // namespace detail

inline data_chunk to_data(fungible const& x)      { return detail::to_data_impl(x); }
inline data_chunk to_data(non_fungible const& x)  { return detail::to_data_impl(x); }
inline data_chunk to_data(both_kinds const& x)    { return detail::to_data_impl(x); }
inline data_chunk to_data(token_data_t const& x)  { return detail::to_data_impl(x); }

} // namespace token::encoding

} // namespace kth::domain::chain
