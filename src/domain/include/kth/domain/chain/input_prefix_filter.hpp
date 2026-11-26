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
//
// A filter of N values is coded the one way N gives: Rice coding with a
// parameter k from 15 down to 1 for N up to 19,196, and a bitmap of the
// 65,536 values from 19,197 on. For each N it is the coding with the fewest
// expected bits for N distinct values drawn at random (the larger k on the
// one tie, at N = 1); past 19,196 every k takes more than the bitmap's 65,536
// bits. k = 0 is never used. The largest filter a block can have is 8,899
// bytes: a Rice coding of 19,196 values whose gaps were chosen to be large.

/// The inputs of a transaction a sender may grind: the first 30.
inline constexpr size_t input_prefix_inputs = 30;

/// The number of values a prefix can take: 2^16.
inline constexpr uint32_t input_prefix_values = 1u << 16;

/// The largest Rice parameter, the one of a filter of one value.
inline constexpr uint8_t max_input_prefix_rice_parameter = 15;

/// The byte after the count of a filter coded as a bitmap.
inline constexpr uint8_t input_prefix_bitmap = 16;

/// The first count coded as a bitmap.
inline constexpr size_t input_prefix_bitmap_from = 19197;

/// The bitmap: a bit for each value, 8,192 bytes.
inline constexpr size_t input_prefix_bitmap_bytes = input_prefix_values / 8;

/// The sorted distinct values of a block's inputs.
struct KD_API input_prefix_filter {
    std::vector<uint16_t> values;

    friend bool operator==(input_prefix_filter const&, input_prefix_filter const&) = default;
};

/// Why bytes were refused as an input prefix filter, or a prefix was refused.
enum class input_prefix_filter_error {
    /// The bytes end before the count, the byte after it, the last value or
    /// the end of the bitmap.
    truncated,
    /// The count is not in its shortest form, or is above 65,536, the number
    /// of values there are.
    count,
    /// A byte after the count other than the one the count gives: the Rice
    /// parameter of another count, the bitmap's 16 for fewer than 19,197
    /// values, a Rice parameter for 19,197 values or more, or anything above 16.
    rice_parameter,
    /// A value after the first that is not above the one before it: a gap of 0.
    repeated_value,
    /// A value above 65,535.
    value_too_large,
    /// A bit after the last value, in its byte, that is not zero.
    padding,
    /// A byte after the one that holds the last value, or after the bitmap.
    trailing_bytes,
    /// A bitmap whose bits set are not as many as the count.
    bitmap_count,
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

/// The byte after the count of a filter of `count` values: the Rice
/// parameter k, 15 for 1 value, 14 for 2, 13 from 3, 12 from 7, 11 from 15,
/// 10 from 30, 9 from 61, 8 from 123, 7 from 246, 6 from 494, 5 from 992,
/// 4 from 1,997, 3 from 4,051, 2 from 8,356 and 1 from 18,055 to 19,196; then
/// input_prefix_bitmap (16) from 19,197 to 65,536. 0 for no values and for
/// more than 65,536. One coding for each count, so each filter has one
/// encoding.
[[nodiscard]]
KD_API
uint8_t input_prefix_rice_parameter(size_t count);

/// Served bytes: the count as a CompactSize and, when there are values, the
/// byte the count gives. Up to 19,196 values it is the Rice parameter k, then
/// each value's gap from the one before (from 0 for the first) Rice coded:
/// the gap shifted right by k in one-bits, a zero-bit, then the low k bits of
/// the gap; most significant bit first, the last byte padded with zero-bits.
/// From 19,197 values it is 16, then the bitmap: value v is the bit
/// 0x80 >> (v % 8) of byte v / 8.
[[nodiscard]]
KD_API
data_chunk to_data(input_prefix_filter const& filter);

/// Reads served bytes, and refuses any that are not the one encoding of a
/// set: a byte after the count other than the one the count gives, a gap of
/// 0 after the first value, a value above 65,535, fewer or more values than
/// the count, a padding bit set, a bitmap shorter or longer than 8,192 bytes
/// or with another number of bits set than the count, trailing bytes, and a
/// count above 65,536 or not in its shortest form.
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
