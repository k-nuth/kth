// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_INPUT_HPP
#define KTH_DOMAIN_CHAIN_INPUT_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <shared_mutex>
#endif

#include <kth/domain/chain/input_basis.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

struct KD_API input : input_basis {
public:
    using list = std::vector<input>;

    // Constructors.
    //-------------------------------------------------------------------------
    input() = default;
    using input_basis::input_basis; // inherit constructors from input_basis

    explicit
    input(input_basis const& x);

    explicit
    input(input_basis&& x) noexcept;

    // Special member functions.
    //-------------------------------------------------------------------------

    input(input const& x);
    input(input&& x) noexcept;
    input& operator=(input&& x) noexcept;
    input& operator=(input const& x);

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<input> from_data(byte_reader& reader, bool wire);

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------
    void set_script(chain::script const& value);
    void set_script(chain::script&& value);

    /// The first payment address extracted (may be invalid).
    wallet::payment_address address() const;

    /// The payment addresses extracted from this input as a standard script.
    wallet::payment_address::list addresses() const;

// protected:
    void reset();
protected:
    void invalidate_cache() const;

private:
    using addresses_ptr = std::shared_ptr<wallet::payment_address::list>;
    addresses_ptr addresses_cache() const;

#if ! defined(__EMSCRIPTEN__)
    mutable upgrade_mutex mutex_;
#else
    mutable std::shared_mutex mutex_;
#endif

    mutable addresses_ptr addresses_;
};

} // namespace kth::domain::chain

//#include <kth/domain/concepts_undef.hpp>

#endif