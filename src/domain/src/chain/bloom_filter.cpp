// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/bloom_filter.hpp>

#include <algorithm>
#include <array>
#include <cmath>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/machine/script_pattern.hpp>

namespace kth::domain::chain {

namespace {

constexpr double ln2_squared = 0.4804530139182014246671025263266649717305529515945455;
constexpr double ln2 = 0.6931471805599453094172321214581765680755001343602552;

constexpr uint32_t rotate_left(uint32_t value, int by) noexcept {
    return (value << by) | (value >> (32 - by));
}

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

} // namespace

uint32_t murmur_hash3(uint32_t seed, byte_span data) {
    constexpr uint32_t c1 = 0xcc9e2d51;
    constexpr uint32_t c2 = 0x1b873593;
    auto hash = seed;
    auto const blocks = data.size() / 4;
    for (size_t i = 0; i < blocks; ++i) {
        auto k = uint32_t(data[i * 4]) | (uint32_t(data[i * 4 + 1]) << 8)
            | (uint32_t(data[i * 4 + 2]) << 16) | (uint32_t(data[i * 4 + 3]) << 24);
        k *= c1;
        k = rotate_left(k, 15);
        k *= c2;
        hash ^= k;
        hash = rotate_left(hash, 13);
        hash = hash * 5 + 0xe6546b64;
    }
    auto const tail = data.subspan(blocks * 4);
    uint32_t k = 0;
    if (tail.size() >= 3) k ^= uint32_t(tail[2]) << 16;
    if (tail.size() >= 2) k ^= uint32_t(tail[1]) << 8;
    if (tail.size() >= 1) {
        k ^= uint32_t(tail[0]);
        k *= c1;
        k = rotate_left(k, 15);
        k *= c2;
        hash ^= k;
    }
    hash ^= uint32_t(data.size());
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    return hash;
}

bloom_filter::bloom_filter(data_chunk bits, uint32_t hash_functions, uint32_t tweak, uint8_t flags)
    : bits_(std::move(bits))
    , hash_functions_(hash_functions)
    , tweak_(tweak)
    , flags_(flags)
{}

bloom_filter bloom_filter::sized(size_t elements, double false_positive_rate, uint32_t tweak, uint8_t flags) {
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

uint32_t bloom_filter::bit_of(uint32_t function, byte_span data) const {
    // The multiplier makes the seeds of the hash functions differ in many bits.
    return murmur_hash3(function * 0xfba4c795 + tweak_, data) % uint32_t(bits_.size() * 8);
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
        // An operation that does not parse carries no data, so nothing past
        // the last whole push of a script is matched.
        for (auto const& op : script.operations()) {
            auto const& data = op.data();
            if (data.empty() || ! contains(data)) continue;
            found = true;
            auto const update = flags_ & update_mask;
            if (update == update_all) {
                insert_outpoint(point(id, uint32_t(i)));
            } else if (update == update_p2pubkey_only) {
                auto const pattern = script.output_pattern();
                if (pattern == machine::script_pattern::pay_to_public_key
                    || pattern == machine::script_pattern::pay_to_multisig) {
                    insert_outpoint(point(id, uint32_t(i)));
                }
            }
            break;
        }
    }
    return found;
}

bool bloom_filter::match_inputs(transaction const& tx) const {
    for (auto const& input : tx.inputs()) {
        if (contains_outpoint(input.previous_output())) return true;
        for (auto const& op : input.script().operations()) {
            auto const& data = op.data();
            if ( ! data.empty() && contains(data)) return true;
        }
    }
    return false;
}

bool bloom_filter::is_relevant_and_update(transaction const& tx) {
    auto const outputs = match_and_insert_outputs(tx);
    return match_inputs(tx) || outputs;
}

} // namespace kth::domain::chain
