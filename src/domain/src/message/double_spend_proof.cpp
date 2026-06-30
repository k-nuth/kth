// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/double_spend_proof.hpp>

#include <initializer_list>

// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::message {

std::string const double_spend_proof::command = "dsproof-beta";
uint32_t const double_spend_proof::version_minimum = version::level::minimum;
uint32_t const double_spend_proof::version_maximum = version::level::maximum;

double_spend_proof::double_spend_proof(chain::output_point const& out_point, spender const& spender1, spender const& spender2)
    : out_point_(out_point)
    , spender1_(spender1)
    , spender2_(spender2)
{}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<double_spend_proof::spender> double_spend_proof::spender::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const version = reader.read_little_endian<uint32_t>();
    if ( ! version) {
        return std::unexpected(version.error());
    }
    auto const out_sequence = reader.read_little_endian<uint32_t>();
    if ( ! out_sequence) {
        return std::unexpected(out_sequence.error());
    }
    auto const locktime = reader.read_little_endian<uint32_t>();
    if ( ! locktime) {
        return std::unexpected(locktime.error());
    }
    auto const prev_outs_hash = read_hash(reader);
    if ( ! prev_outs_hash) {
        return std::unexpected(prev_outs_hash.error());
    }
    auto const sequence_hash = read_hash(reader);
    if ( ! sequence_hash) {
        return std::unexpected(sequence_hash.error());
    }
    auto const outputs_hash = read_hash(reader);
    if ( ! outputs_hash) {
        return std::unexpected(outputs_hash.error());
    }
    auto const push_data = reader.read_remaining_bytes();
    if ( ! push_data) {
        return std::unexpected(push_data.error());
    }

    return spender {
        *version,
        *out_sequence,
        *locktime,
        *prev_outs_hash,
        *sequence_hash,
        *outputs_hash,
        data_chunk(push_data->begin(), push_data->end())
    };
}

// static
expect<double_spend_proof> double_spend_proof::from_data(byte_reader& reader, uint32_t /*version*/) {
    auto const out_point = chain::output_point::from_data(reader, true);
    if ( ! out_point) {
        return std::unexpected(out_point.error());
    }
    auto const spender1 = spender::from_data(reader, 0);
    if ( ! spender1) {
        return std::unexpected(spender1.error());
    }

    auto const spender2 = spender::from_data(reader, 0);
    if ( ! spender2) {
        return std::unexpected(spender2.error());
    }

    return double_spend_proof(*out_point, *spender1, *spender2);
}

// Serialization.
//-----------------------------------------------------------------------------

expect<void> double_spend_proof::to_data(byte_writer& writer, uint32_t /*version*/) const {
    if (auto r = out_point_.to_data(writer, true); ! r) return r;
    if (auto r = spender1_.to_data(writer); ! r) return r;
    return spender2_.to_data(writer);
}

hash_digest double_spend_proof::hash() const {
    return sha256_hash(kth::to_data_chunk(*this, uint32_t{0}));
}

[[nodiscard]]
chain::output_point const& double_spend_proof::out_point() const {
    return out_point_;
}

void double_spend_proof::set_out_point(chain::output_point const& x) {
    out_point_ = x;
}

[[nodiscard]]
double_spend_proof::spender const& double_spend_proof::spender1() const {
    return spender1_;
}

void double_spend_proof::set_spender1(double_spend_proof::spender const& x) {
    spender1_ = x;
}

[[nodiscard]]
double_spend_proof::spender const& double_spend_proof::spender2() const {
    return spender2_;
}

void double_spend_proof::set_spender2(double_spend_proof::spender const& x) {
    spender2_ = x;
}

hash_digest hash(double_spend_proof const& x) {
    return x.hash();
}

} // namespace kth::domain::message
