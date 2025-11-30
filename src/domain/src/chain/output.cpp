// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/output.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>

#include <kth/domain/constants.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

using namespace kth::domain::wallet;

// This is a consensus critical value that must be set on reset.
uint64_t const output::not_found = sighash_null_value;

// This is a non-consensus sentinel used to indicate an output is unspent.
uint32_t const output::validation::not_spent = max_uint32;

// Constructors.
//-----------------------------------------------------------------------------

output::output(output_basis const& x)
    : output_basis(x)
{}

output::output(output_basis&& x) noexcept
    : output_basis(std::move(x))
{}

output::output(output const& x)
    : output_basis(x)
    , addresses_(x.addresses_cache())
    , validation(x.validation)
{}

output::output(output&& x) noexcept
    : output_basis(std::move(x))
    , addresses_(x.addresses_cache())
    , validation(x.validation)
{}

output& output::operator=(output const& x) {
    output_basis::operator=(x);
    addresses_ = x.addresses_cache();
    validation = x.validation;
    return *this;
}

output& output::operator=(output&& x) noexcept {
    output_basis::operator=(std::move(static_cast<output_basis&&>(x)));
    addresses_ = x.addresses_cache();
    validation = x.validation;
    return *this;
}

// Private cache access for copy/move construction.
output::addresses_ptr output::addresses_cache() const {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    shared_lock lock(mutex_);

    return addresses_;
    ///////////////////////////////////////////////////////////////////////////
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<output> output::from_data(byte_reader& reader, bool wire) {
    uint32_t spender_height = validation::not_spent;
    if ( ! wire) {
        auto const height = reader.read_little_endian<uint32_t>();
        if ( ! height) {
            return std::unexpected(height.error());
        }
        spender_height = *height;
    }

    auto basis = output_basis::from_data(reader, wire);
    if ( ! basis) {
        return std::unexpected(basis.error());
    }
    output result(std::move(*basis));
    result.validation.spender_height = spender_height;
    return result;
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk output::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, wire);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void output::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Size.
//-----------------------------------------------------------------------------

size_t output::serialized_size(bool wire) const {
    // validation.spender_height is size_t stored as uint32_t.
    return (wire ? 0 : sizeof(uint32_t))
            + output_basis::serialized_size(wire);
}

// Accessors.
//-----------------------------------------------------------------------------

void output::set_script(chain::script const& value) {
    output_basis::set_script(value);
    invalidate_cache();
}

void output::set_script(chain::script&& value) {
    output_basis::set_script(std::move(value));
    invalidate_cache();
}

// protected
void output::invalidate_cache() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (addresses_) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        addresses_.reset();
        //---------------------------------------------------------------------
        mutex_.unlock_and_lock_upgrade();
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
#else
    {
        std::shared_lock lock(mutex_);
        if ( ! addresses_) {
            return;
        }
    }

    std::unique_lock lock(mutex_);
    addresses_.reset();
#endif
}

payment_address output::address(bool testnet /*= false*/) const {
    if (testnet) {
        return address(wallet::payment_address::testnet_p2kh, wallet::payment_address::testnet_p2sh);
    }
    return address(wallet::payment_address::mainnet_p2kh, wallet::payment_address::mainnet_p2sh);
}

payment_address output::address(uint8_t p2kh_version, uint8_t p2sh_version) const {
    auto const value = addresses(p2kh_version, p2sh_version);
    return value.empty() ? payment_address{} : value.front();
}

payment_address::list output::addresses(uint8_t p2kh_version, uint8_t p2sh_version) const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if ( ! addresses_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        mutex_.unlock_upgrade_and_lock();
        addresses_ = std::make_shared<payment_address::list>(payment_address::extract_output(script(), p2kh_version, p2sh_version));
        mutex_.unlock_and_lock_upgrade();
        //---------------------------------------------------------------------
    }

    auto addresses = *addresses_;
    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
#else
    {
        std::shared_lock lock(mutex_);
        if (addresses_) {
            return *addresses_;
        }
    }

    std::unique_lock lock(mutex_);
    if ( ! addresses_) {
        addresses_ = std::make_shared<payment_address::list>(payment_address::extract_output(script(), p2kh_version, p2sh_version));
    }
    auto addresses = *addresses_;
#endif
    return addresses;
}

} // namespace kth::domain::chain
