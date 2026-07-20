// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/output.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>

#include <kth/domain/constants.hpp>
#include <kth/domain/wallet/payment_address.hpp>

namespace kth::domain::chain {

using namespace kth::domain::wallet;

// Constructors.
//-----------------------------------------------------------------------------

output::output(uint64_t value, chain::script const& script, token_data_opt const& token_data)
    : value_(value)
    , script_(script)
    , token_data_(token_data)
{}

output::output(uint64_t value, chain::script&& script, token_data_opt&& token_data)
    : value_(value)
    , script_(std::move(script))
    , token_data_(std::move(token_data))
{}

// Operators.
//-----------------------------------------------------------------------------

bool operator==(output const& a, output const& b) {
    return a.value_ == b.value_
        && a.script_ == b.script_
        && a.token_data_ == b.token_data_;
}

// Serialization.
//-----------------------------------------------------------------------------

void output::reset() {
    value_ = output::not_found;
    script_.reset();
    token_data_.reset();
}

// Empty scripts are valid, validation relies on not_found only.
bool output::is_valid() const {
    return value_ != output::not_found && chain::is_valid(token_data_);
}

// static
expect<output> output::from_data(byte_reader& reader, bool wire) {
    uint32_t spender_height = validation_t::not_spent;
    if ( ! wire) {
        auto const height = reader.read_little_endian<uint32_t>();
        if ( ! height) {
            return std::unexpected(height.error());
        }
        spender_height = *height;
    }

    auto const value = reader.read_little_endian<uint64_t>();
    if ( ! value) {
        return std::unexpected(value.error());
    }

    auto const script_size_exp = reader.read_size_little_endian();
    if ( ! script_size_exp) {
        return std::unexpected(script_size_exp.error());
    }
    auto script_size = *script_size_exp;

    // Minimum token prefix: 0xef (1) + category_id (32) + bitfield (1) = 34 bytes.
    static constexpr size_t min_token_prefix_size = 1 + 32 + 1;
    token_data_opt token_data = std::nullopt;
    auto const has_token_data = script_size >= min_token_prefix_size && [&] {
        auto const token_prefix_byte = reader.peek_byte();
        return token_prefix_byte && *token_prefix_byte == chain::encoding::PREFIX_BYTE;
    }();
    if (has_token_data) {
        reader.unsafe_skip_byte(); // skip prefix byte (safe: peek_byte succeeded above)
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

    output result{
        *value,
        std::move(*script),
        std::move(token_data)
    };
    result.validation.spender_height = spender_height;
    return result;
}

expect<void> output::to_data(byte_writer& writer, bool wire) const {
    auto const start = writer.position();
    if ( ! wire) {
        auto const height32 = *safe_unsigned<uint32_t>(validation.spender_height);
        if (auto r = writer.write_little_endian<uint32_t>(height32); ! r) {
            return r;
        }
    }

    if (auto r = writer.write_little_endian<uint64_t>(value_); ! r) {
        return r;
    }

    if ( ! token_data_.has_value()) {
        auto const r = script_.to_data(writer, true);
        KTH_CONTRACT(writer.position() - start == serialized_size(wire));
        return r;
    }

    auto const size = token::encoding::serialized_size(token_data_) + script_.serialized_size(false) + 1;
    if (auto r = writer.write_variable_little_endian(size); ! r) {
        return r;
    }

    if (auto r = writer.write_byte(chain::encoding::PREFIX_BYTE); ! r) {
        return r;
    }
    if (auto r = token::encoding::to_data(writer, token_data_.value()); ! r) {
        return r;
    }
    auto const r = script_.to_data(writer, false);
    KTH_CONTRACT(writer.position() - start == serialized_size(wire));
    return r;
}

// Size.
//-----------------------------------------------------------------------------

size_t output::serialized_size(bool wire) const {
    // Wire format:
    //   value(8) + script                     (no token)
    //   value(8) + varint(wrapper_size) + prefix_byte(1) + token + script_bytes  (with token)
    // The tokenized branch prefixes the whole wrapper (prefix+token+script_bytes)
    // with a single varint, NOT the bare script bytes. Using
    // `script_.serialized_size(true)` here would embed `varint(script_bytes)`
    // which is a different length than `varint(wrapper_size)` whenever the
    // two values fall on opposite sides of a varint boundary (e.g. one fits
    // in 1 byte, the other needs 3). That mismatch used to be masked by the
    // growable `data_sink`; the fixed-size `byte_writer` truncates instead.
    size_t wire_size;
    if (token_data_.has_value()) {
        auto const token_size = token::encoding::serialized_size(token_data_);
        auto const script_size = script_.serialized_size(false);
        auto const wrapper_size = 1 /*prefix*/ + token_size + script_size;
        wire_size = sizeof(value_)
                  + kth::size_variable_integer(wrapper_size)
                  + wrapper_size;
    } else {
        wire_size = sizeof(value_) + script_.serialized_size(true);
    }

    return (wire ? 0 : sizeof(uint32_t)) + wire_size;
}

// Accessors.
//-----------------------------------------------------------------------------

uint64_t output::value() const {
    return value_;
}

void output::set_value(uint64_t x) {
    value_ = x;
}

chain::script& output::script() {
    return script_;
}

chain::script const& output::script() const {
    return script_;
}

void output::set_script(chain::script const& x) {
    script_ = x;
}

void output::set_script(chain::script&& x) {
    script_ = std::move(x);
}

chain::token_data_opt& output::token_data() {
    return token_data_;
}

chain::token_data_opt const& output::token_data() const {
    return token_data_;
}

void output::set_token_data(chain::token_data_opt const& x) {
    token_data_ = x;
}

void output::set_token_data(chain::token_data_opt&& x) {
    token_data_ = std::move(x);
}

std::optional<payment_address> output::address(bool testnet /*= false*/) const {
    if (testnet) {
        return address(wallet::payment_address::testnet_p2kh, wallet::payment_address::testnet_p2sh);
    }
    return address(wallet::payment_address::mainnet_p2kh, wallet::payment_address::mainnet_p2sh);
}

std::optional<payment_address> output::address(uint8_t p2kh_version, uint8_t p2sh_version) const {
    auto const value = addresses(p2kh_version, p2sh_version);
    if (value.empty()) {
        return std::nullopt;
    }
    return value.front();
}

payment_address::list output::addresses(uint8_t p2kh_version, uint8_t p2sh_version) const {
    return payment_address::extract_output(script_, p2kh_version, p2sh_version);
}

// Validation helpers.
//-----------------------------------------------------------------------------

size_t output::signature_operations(bool /*bip141*/) const {
    return script_.sigops(false);
}

bool output::is_dust(uint64_t minimum_output_value) const {
    // If provably unspendable it does not expand the unspent output set.
    return value_ < minimum_output_value && !script_.is_unspendable();
}

} // namespace kth::domain::chain
