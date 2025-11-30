// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/output_basis.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>

#include <kth/domain/constants.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

// Constructors.
//-----------------------------------------------------------------------------

output_basis::output_basis(uint64_t value, chain::script const& script, token_data_opt const& token_data)
    : value_(value)
    , script_(script)
    , token_data_(token_data)
{}

output_basis::output_basis(uint64_t value, chain::script&& script, token_data_opt&& token_data)
    : value_(value)
    , script_(std::move(script))
    , token_data_(std::move(token_data))
{}

//-----------------------------------------------------------------------------

// protected
void output_basis::reset() {
    value_ = output_basis::not_found;
    script_.reset();
    token_data_.reset();
}

// Empty scripts are valid, validation relies on not_found only.
bool output_basis::is_valid() const {
    return value_ != output_basis::not_found &&
           chain::is_valid(token_data_);
}

// Deserialization.
//-----------------------------------------------------------------------------

expect<output_basis> output_basis::from_data(byte_reader& reader, bool /*wire*/) {
    auto const value = reader.read_little_endian<uint64_t>();
    if ( ! value) {
        return std::unexpected(value.error());
    }

    auto const script_size_exp = reader.read_size_little_endian();
    if ( ! script_size_exp) {
        return std::unexpected(script_size_exp.error());
    }
    auto script_size = *script_size_exp;

    auto const token_prefix_byte = reader.peek_byte();
    if ( ! token_prefix_byte) {
        return std::unexpected(token_prefix_byte.error());
    }

    token_data_opt token_data = std::nullopt;
    auto const has_token_data = *token_prefix_byte == chain::encoding::PREFIX_BYTE;
    if (has_token_data) {
        reader.skip(1); // skip prefix byte
        auto token = token::encoding::from_data(reader);
        if ( ! token) {
            return std::unexpected(token.error());
        }
        token_data.emplace(std::move(*token));

        script_size -= token::encoding::serialized_size(token_data);
        script_size -= 1; // prefix byte
    }

    auto script = script::from_data_with_size(reader, script_size);
    if ( ! script) {
        return std::unexpected(script.error());
    }

    return output_basis{
        *value,
        std::move(*script),
        std::move(token_data)
    };
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk output_basis::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void output_basis::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Size.
//-----------------------------------------------------------------------------

size_t output_basis::serialized_size(bool /*wire*/) const {
    return sizeof(value_) +
           script_.serialized_size(true) +
           token::encoding::serialized_size(token_data_) +
           size_t(token_data_.has_value());
}

// Accessors.
//-----------------------------------------------------------------------------

uint64_t output_basis::value() const {
    return value_;
}

void output_basis::set_value(uint64_t x) {
    value_ = x;
}

chain::script& output_basis::script() {
    return script_;
}

chain::script const& output_basis::script() const {
    return script_;
}

void output_basis::set_script(chain::script const& x) {
    script_ = x;
}

void output_basis::set_script(chain::script&& x) {
    script_ = std::move(x);
}

chain::token_data_opt& output_basis::token_data() {
    return token_data_;
}

chain::token_data_opt const& output_basis::token_data() const {
    return token_data_;
}

void output_basis::set_token_data(chain::token_data_opt const& x) {
    token_data_ = x;
}

void output_basis::set_token_data(chain::token_data_opt&& x) {
    token_data_ = std::move(x);
}

// Validation helpers.
//-----------------------------------------------------------------------------

size_t output_basis::signature_operations(bool bip141) const {
    return script_.sigops(false);
}

bool output_basis::is_dust(uint64_t minimum_output_value) const {
    // If provably unspendable it does not expand the unspent output set.
    return value_ < minimum_output_value && !script_.is_unspendable();
}

} // namespace kth::domain::chain
