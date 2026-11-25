// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_INPUT_PREFIX_FILTER_HPP
#define KTH_DOMAIN_CHAIN_INPUT_PREFIX_FILTER_HPP

#if ! defined(KTH_WITH_NEUTRINO)
#error "Compact filters are an optional feature: build with the Conan option neutrino=True."
#endif

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

// The input prefix filter of a block: filter type 2 of the compact filter
// messages, for wallets of reusable payment addresses. A payment to such an
// address is found by one of its inputs: the sender grinds one of the first
// 30 inputs of the transaction until the double SHA-256 of the input starts
// with 4 to 16 bits of the receiver's scan key. The filter holds the first two
// bytes of that hash for every input a sender may grind, so a wallet tests it
// for its own prefix, downloads only the blocks that may pay it, and names
// its prefix to no one.
//
// Its values are, for every transaction after the coinbase and for each of
// its first 30 inputs, the first two bytes of the double SHA-256 of the input
// as it stands in the transaction (the outpoint, the script and the sequence),
// read as a big-endian number. The set is exact: a wallet that downloads a
// block builds the same values from it, so a node that serves another set is
// caught.
//
// Everything in it comes from the block itself, so a node builds it for any
// block it holds and keeps nothing for it.

/// The inputs of a transaction a sender may grind: the first 30.
inline constexpr size_t input_prefix_inputs = 30;

/// The number of values a prefix can take: 2^16.
inline constexpr uint32_t input_prefix_values = 1u << 16;

/// The largest Rice parameter, the one of a filter of one value.
inline constexpr uint8_t max_input_prefix_rice_parameter = 15;

/// The number the Rice parameter is chosen from: 65,536 times ln 2, rounded.
inline constexpr uint32_t input_prefix_rice_scale = 45426;

/// The sorted distinct values of a block's inputs.
struct KD_API input_prefix_filter {
    std::vector<uint16_t> values;

    friend bool operator==(input_prefix_filter const&, input_prefix_filter const&) = default;
};

/// Why bytes were refused as an input prefix filter, or a prefix was refused.
enum class input_prefix_filter_error {
    /// The bytes end before the count, the Rice parameter or the last value.
    truncated,
    /// The count is not in its shortest form, or is above 65,536, the number
    /// of values there are.
    count,
    /// A Rice parameter other than the one its count gives (never above 15).
    rice_parameter,
    /// A value after the first that is not above the one before it: a gap of 0.
    repeated_value,
    /// A value above 65,535.
    value_too_large,
    /// A bit after the last value, in its byte, that is not zero.
    padding,
    /// A byte after the one that holds the last value.
    trailing_bytes,
    /// A prefix of more than 16 bits, or with a bit set past its length.
    prefix,
};

/// The value of an input: the first two bytes of the double SHA-256 of the
/// input as it stands in a transaction, most significant first.
[[nodiscard]]
KD_API
uint16_t input_prefix(input const& in);

/// The filter of a block: the values of the first 30 inputs of every
/// transaction after the first, the coinbase, sorted, each once.
[[nodiscard]]
KD_API
input_prefix_filter build_input_prefix_filter(block const& blk);

/// The Rice parameter of a filter of `count` values: the largest k with
/// 2^k <= 45426 / count (integer division), and 0 when that quotient is 0.
/// One parameter for each count, so each filter has one encoding.
[[nodiscard]]
KD_API
uint8_t input_prefix_rice_parameter(size_t count);

/// Served bytes: the count as a CompactSize and, when there are values, the
/// Rice parameter in one byte, then each value's gap from the one before
/// (from 0 for the first) Rice coded: the gap shifted right by k in one-bits,
/// a zero-bit, then the low k bits of the gap; most significant bit first,
/// the last byte padded with zero-bits.
[[nodiscard]]
KD_API
data_chunk to_data(input_prefix_filter const& filter);

/// Reads served bytes, and refuses any that are not the one encoding of a
/// set: a Rice parameter other than its count's, a gap of 0 after the first value, a value
/// above 65,535, fewer or more values than the count, a padding bit set,
/// trailing bytes, and a count not in its shortest form.
[[nodiscard]]
KD_API
std::expected<input_prefix_filter, input_prefix_filter_error>
parse_input_prefix_filter(byte_span data);

/// Whether the filter has a value that starts with the `bits` bits of
/// `prefix` (0 to 16 of them): a value in [prefix << (16 - bits),
/// (prefix + 1) << (16 - bits)). A block with such a value may pay a wallet
/// whose scan key starts with those bits; a block without one does not.
/// A prefix with a bit set past its length is refused, so each prefix has one
/// encoding.
[[nodiscard]]
KD_API
std::expected<bool, input_prefix_filter_error>
has_input_prefix(input_prefix_filter const& filter, uint16_t prefix, uint8_t bits);

/// Double SHA-256 of the served bytes.
[[nodiscard]]
KD_API
hash_digest input_prefix_filter_hash(input_prefix_filter const& filter);

/// Double SHA-256 of the filter hash followed by the previous header. The
/// header before the genesis block is all zeros.
[[nodiscard]]
KD_API
hash_digest input_prefix_filter_header(input_prefix_filter const& filter, hash_digest const& previous_header);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_INPUT_PREFIX_FILTER_HPP
