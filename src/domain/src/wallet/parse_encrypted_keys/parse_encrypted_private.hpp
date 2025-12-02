// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_PARSE_ENCRYPTED_PRIVATE_HPP
#define KTH_PARSE_ENCRYPTED_PRIVATE_HPP

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_key.hpp"

namespace kth::domain::wallet {

// Swap not defined.
class parse_encrypted_private
    : public parse_encrypted_key<2u> {
public:
    static byte_array<prefix_size> prefix_factory(uint8_t address, bool multiplied);

    explicit parse_encrypted_private(encrypted_private const& key);

    bool multiplied() const;
    uint8_t address_version() const;

    quarter_hash data1() const;
    half_hash data2() const;

private:
    bool verify_magic() const;

    static constexpr
    uint8_t default_context_ = 0x42;

    static constexpr
    uint8_t multiplied_context_ = 0x43;

    static
    const byte_array<magic_size> magic_;

    quarter_hash const data1_;
    half_hash const data2_;
};

} // namespace kth::domain::wallet

#endif
