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
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
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

bool double_spend_proof::is_valid() const {
    return out_point_.is_valid()
        && spender1_.is_valid()
        && spender2_.is_valid();
}

void double_spend_proof::reset() {
    out_point_.reset();
    spender1_.reset();
    spender2_.reset();
}


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

data_chunk double_spend_proof::to_data(size_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void double_spend_proof::to_data(size_t version, data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

hash_digest double_spend_proof::hash() const {
    return sha256_hash(to_data(0));
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
