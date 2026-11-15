// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/bloom_filter.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/murmur_hash.hpp>

namespace kth::domain::chain {

namespace {

constexpr double ln2_squared = 0.4804530139182014246671025263266649717305529515945455;
constexpr double ln2 = 0.6931471805599453094172321214581765680755001343602552;

// The hash as it stands in a transaction, then the index in little-endian.
std::array<uint8_t, hash_size + sizeof(uint32_t)> outpoint_bytes(point const& outpoint) {
    std::array<uint8_t, hash_size + sizeof(uint32_t)> bytes{};
    std::copy(outpoint.hash().begin(), outpoint.hash().end(), bytes.begin());
    auto const index = outpoint.index();
    bytes[hash_size] = uint8_t(index & 0xff);
    bytes[hash_size + 1] = uint8_t((index >> 8) & 0xff);
    bytes[hash_size + 2] = uint8_t((index >> 16) & 0xff);
    bytes[hash_size + 3] = uint8_t((index >> 24) & 0xff);
    return bytes;
}

// Whether `matches` holds for the data of one of the pushes of a script. The
// script is read in place: nothing is copied out of it. An operation that
// does not parse ends the script, so nothing past its last whole push is
// looked at.
template <typename Predicate>
bool any_push(byte_span script, Predicate matches) {
    byte_reader reader(script);
    while (reader.remaining_size() != 0) {
        auto const code = reader.read_byte();
        if ( ! code) return false;
        auto const size = machine::operation::read_data_size(machine::opcode(*code), reader);
        if ( ! size) return false;
        auto const data = reader.read_bytes(*size);
        if ( ! data) return false;
        if ( ! data->empty() && matches(*data)) return true;
    }
    return false;
}

// A public key as the nodes that serve these filters tell one: by its length
// and its first byte. The hybrid form (06, 07) counts, which the standard
// output patterns leave out.
bool is_key(byte_span data) {
    if (data.size() == ec_compressed_size) return data[0] == 0x02 || data[0] == 0x03;
    if (data.size() == ec_uncompressed_size) return data[0] == 0x04 || data[0] == 0x06 || data[0] == 0x07;
    return false;
}

// Whether an output script pays to a public key or to a bare multisig: the
// outputs whose outpoint update_p2pubkey_only adds to the filter.
bool pays_to_public_keys(script const& output_script) {
    using machine::opcode;
    using machine::operation;
    auto const ops = output_script.operations();
    auto const count = ops.size();

    // <key> OP_CHECKSIG, the key pushed by its own length.
    if (count == 2) {
        return ops[1].code() == opcode::checksig && is_key(ops[0].data())
            && uint8_t(ops[0].code()) == ops[0].data().size();
    }

    // OP_m <key>... OP_n OP_CHECKMULTISIG, with n keys and m of them needed.
    if (count < 4 || ops[count - 1].code() != opcode::checkmultisig) return false;
    auto const required = ops[0].code();
    auto const total = ops[count - 2].code();
    if ( ! operation::is_positive(required) || ! operation::is_positive(total)) return false;
    auto const keys = size_t(operation::opcode_to_positive(total));
    if (keys != count - 3 || operation::opcode_to_positive(required) > keys) return false;
    return std::all_of(ops.begin() + 1, ops.end() - 2,
        [](operation const& op) { return is_key(op.data()); });
}

} // namespace

bloom_filter::bloom_filter(data_chunk bits, uint32_t hash_functions, uint32_t tweak, uint8_t flags)
    : bits_(std::move(bits))
    , hash_functions_(hash_functions)
    , tweak_(tweak)
    , flags_(flags)
{}

expect<bloom_filter> bloom_filter::sized(size_t elements, double false_positive_rate, uint32_t tweak, uint8_t flags) {
    // A rate is a probability other than the two certainties. Written so that
    // a NaN, which compares false with everything, is refused too.
    if ( ! (false_positive_rate > 0.0 && false_positive_rate < 1.0)) {
        return std::unexpected(error::illegal_value);
    }
    auto const count = double(std::max(size_t{1}, elements));
    // The size that holds that many elements at that rate, and the number of
    // hash functions that is best for it, each within its limit.
    auto const wanted_bits = -1.0 / ln2_squared * count * std::log(false_positive_rate);
    auto const bit_count = std::min(wanted_bits, double(max_size * 8));
    data_chunk bits(bit_count <= 0.0 ? size_t{0} : size_t(bit_count) / 8, 0);
    auto const functions = bits.empty() ? uint32_t{0}
        : uint32_t(std::min(double(bits.size() * 8) / count * ln2, double(max_hash_functions)));
    return bloom_filter(std::move(bits), functions, tweak, flags);
}

bool bloom_filter::within_limits() const {
    return bits_.size() <= max_size && hash_functions_ <= max_hash_functions;
}

size_t bloom_filter::bit_of(uint32_t function, byte_span data) const {
    // The multiplier makes the seeds of the hash functions differ in many bits.
    // The number of bits is not narrowed: a filter of any size has one.
    return murmur_hash3(function * 0xfba4c795 + tweak_, data) % (bits_.size() * 8);
}

void bloom_filter::insert(byte_span data) {
    if (bits_.empty()) return;
    for (uint32_t i = 0; i < hash_functions_; ++i) {
        auto const bit = bit_of(i, data);
        bits_[bit >> 3] |= uint8_t(1u << (bit & 7));
    }
}

void bloom_filter::insert_outpoint(point const& outpoint) {
    insert(outpoint_bytes(outpoint));
}

bool bloom_filter::contains(byte_span data) const {
    if (bits_.empty()) return true;
    for (uint32_t i = 0; i < hash_functions_; ++i) {
        auto const bit = bit_of(i, data);
        if ((bits_[bit >> 3] & uint8_t(1u << (bit & 7))) == 0) return false;
    }
    return true;
}

bool bloom_filter::contains_outpoint(point const& outpoint) const {
    return contains(outpoint_bytes(outpoint));
}

bool bloom_filter::match_and_insert_outputs(transaction const& tx) {
    auto const id = tx.hash();
    auto found = contains(id);
    auto const& outputs = tx.outputs();
    for (size_t i = 0; i < outputs.size(); ++i) {
        auto const& script = outputs[i].script();
        if ( ! any_push(script.bytes(), [this](byte_span data) { return contains(data); })) continue;
        found = true;
        auto const update = flags_ & update_mask;
        if (update == update_all) {
            insert_outpoint(point(id, uint32_t(i)));
        } else if (update == update_p2pubkey_only && pays_to_public_keys(script)) {
            insert_outpoint(point(id, uint32_t(i)));
        }
    }
    return found;
}

bool bloom_filter::match_inputs(transaction const& tx) const {
    for (auto const& input : tx.inputs()) {
        if (contains_outpoint(input.previous_output())) return true;
        if (any_push(input.script().bytes(), [this](byte_span data) { return contains(data); })) return true;
    }
    return false;
}

bool bloom_filter::is_relevant_and_update(transaction const& tx) {
    auto const outputs = match_and_insert_outputs(tx);
    return match_inputs(tx) || outputs;
}

} // namespace kth::domain::chain
