// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_PARSE_ENCRYPTED_PUBLIC_HPP
#define KTH_PARSE_ENCRYPTED_PUBLIC_HPP

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_key.hpp"

namespace kth::domain::wallet {

// Swap not defined.
class parse_encrypted_public
    : public parse_encrypted_key<5u> {
public:
    static
    byte_array<prefix_size> prefix_factory(uint8_t address);

    explicit parse_encrypted_public(encrypted_public const& key);

    uint8_t address_version() const;

    one_byte sign() const;
    hash_digest data() const;

private:
    bool verify_magic() const;

    static constexpr
    uint8_t default_context_ = 0x9a;

    static
    const byte_array<magic_size> magic_;

    one_byte const sign_;
    hash_digest const data_;
};

} // namespace kth::domain::wallet

#endif
