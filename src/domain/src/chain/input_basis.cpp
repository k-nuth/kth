// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/input_basis.hpp>

#include <algorithm>
#include <sstream>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

using namespace kth::domain::machine;

// Constructors.
//-----------------------------------------------------------------------------

input_basis::input_basis(output_point&& previous_output, chain::script&& script, uint32_t sequence)
    : previous_output_(std::move(previous_output)),
      script_(std::move(script)),
      sequence_(sequence)
{}

input_basis::input_basis(output_point const& previous_output, chain::script const& script, uint32_t sequence)
    : previous_output_(previous_output),
      script_(script),
      sequence_(sequence)
{}

// Operators.
//-----------------------------------------------------------------------------

void input_basis::reset() {
    previous_output_.reset();
    script_.reset();
    sequence_ = 0;
}

// Since empty scripts and zero sequence are valid this relies on the prevout.
bool input_basis::is_valid() const {
    return sequence_ != 0
        || previous_output_.is_valid()
        || script_.is_valid();
}

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<input_basis> input_basis::from_data(byte_reader& reader, bool wire) {
    auto point = output_point::from_data(reader, wire);
    if ( ! point) {
        return std::unexpected(point.error());
    }
    auto script = script::from_data(reader, true);
    if ( ! script) {
        return std::unexpected(script.error());
    }
    auto sequence = reader.read_little_endian<uint32_t>();
    if ( ! sequence) {
        return std::unexpected(sequence.error());
    }
    return input_basis{std::move(*point), std::move(*script), *sequence};
}

data_chunk input_basis::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void input_basis::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Size.
//-----------------------------------------------------------------------------

size_t input_basis::serialized_size(bool wire) const {
    return previous_output_.serialized_size(wire)
        + script_.serialized_size(true)
        + sizeof(sequence_);
}


// Accessors.
//-----------------------------------------------------------------------------

output_point& input_basis::previous_output() {
    return previous_output_;
}

output_point const& input_basis::previous_output() const {
    return previous_output_;
}

void input_basis::set_previous_output(output_point const& value) {
    previous_output_ = value;
}

void input_basis::set_previous_output(output_point&& value) {
    previous_output_ = std::move(value);
}

chain::script& input_basis::script() {
    return script_;
}

chain::script const& input_basis::script() const {
    return script_;
}

void input_basis::set_script(chain::script const& value) {
    script_ = value;
}

void input_basis::set_script(chain::script&& value) {
    script_ = std::move(value);
}

uint32_t input_basis::sequence() const {
    return sequence_;
}

void input_basis::set_sequence(uint32_t value) {
    sequence_ = value;
}

// Validation helpers.
//-----------------------------------------------------------------------------

bool input_basis::is_final() const {
    return sequence_ == max_input_sequence;
}

bool input_basis::is_locked(size_t block_height, uint32_t median_time_past) const {
    if ((sequence_ & relative_locktime_disabled) != 0) {
        return false;
    }

    // bip68: a minimum block-height constraint over the input's age.
    auto const minimum = (sequence_ & relative_locktime_mask);
    auto const& prevout = previous_output_.validation;

    if ((sequence_ & relative_locktime_time_locked) != 0) {
        // Median time past must be monotonically-increasing by block.
        KTH_ASSERT(median_time_past >= prevout.median_time_past);
        auto const age_seconds = median_time_past - prevout.median_time_past;
        return age_seconds < (minimum << relative_locktime_seconds_shift);
    }

    KTH_ASSERT(block_height >= prevout.height);
    auto const age_blocks = block_height - prevout.height;
    return age_blocks < minimum;
}

// This requires that previous outputs have been populated.
// This cannot overflow because each total is limited by max ops.
size_t input_basis::signature_operations(bool bip16, bool bip141) const {
    auto const& prevout = previous_output_.validation.cache.script();
    size_t const sigops_factor = 1U;
    // Count heavy sigops in the input script.
    auto sigops = script_.sigops(false) * sigops_factor;

    if (bip16) {
        auto const embedded = extract_embedded_script();
        if (embedded) {
            // Add heavy sigops in the embedded script (bip16).
            return sigops + embedded->sigops(true) * sigops_factor;
        }
    }

    return sigops;
}

// // This requires that previous outputs have been populated.
// bool input_basis::extract_embedded_script(chain::script& out) const {
//     ////KTH_ASSERT(previous_output_.is_valid());
//     auto const& ops = script_.operations();
//     auto const& prevout_script = previous_output_.validation.cache.script();

//     // There are no embedded sigops when the prevout script is not p2sh.
//     if ( ! prevout_script.is_pay_to_script_hash(rule_fork::bip16_rule)) {
//         return false;
//     }

//     // There are no embedded sigops when the input script is not push only.
//     // The first operations access must be method-based to guarantee the cache.
//     if (ops.empty() || !script::is_relaxed_push(ops)) {
//         return false;
//     }

//     // Parse the embedded script from the last input script item (data).
//     // This cannot fail because there is no prefix to invalidate the length.
//     return entity_from_data(out, ops.back().data(), false);
// }

// This requires that previous outputs have been populated.
expect<chain::script> input_basis::extract_embedded_script() const {
    ////KTH_ASSERT(previous_output_.is_valid());
    auto const& ops = script_.operations();
    auto const& prevout_script = previous_output_.validation.cache.script();

    // There are no embedded sigops when the prevout script is not p2sh or p2sh32.
    if ( ! prevout_script.is_pay_to_script_hash(rule_fork::bip16_rule) &&
         ! prevout_script.is_pay_to_script_hash_32(rule_fork::bch_gauss)) {
            return std::unexpected(error::invalid_script_type);
    }

    // There are no embedded sigops when the input script is not push only.
    // The first operations access must be method-based to guarantee the cache.
    if (ops.empty() || !script::is_relaxed_push(ops)) {
        return std::unexpected(error::script_not_push_only);
    }

    // Parse the embedded script from the last input script item (data).
    // This cannot fail because there is no prefix to invalidate the length.
    byte_reader reader(std::span<uint8_t const>(ops.back().data()));
    return script::from_data(reader, false);
}


} // namespace kth::domain::chain
