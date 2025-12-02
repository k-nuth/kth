// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_PARSE_ENCRYPTED_TOKEN_HPP
#define KTH_PARSE_ENCRYPTED_TOKEN_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_key.hpp"

namespace kth::domain::wallet {

// Swap not defined.
class parse_encrypted_token
    : public parse_encrypted_prefix<8u> {
public:
    static
    byte_array<prefix_size> prefix_factory(bool lot_sequence);

    explicit parse_encrypted_token(encrypted_token const& value);

    bool lot_sequence() const;
    hash_digest data() const;
    ek_entropy entropy() const;
    one_byte sign() const;

private:
    bool verify_context() const;
    bool verify_magic() const;

    static constexpr
    uint8_t lot_context_ = 0x51;

    static constexpr
    uint8_t default_context_ = 0x53;

    static
    const byte_array<magic_size> magic_;

    ek_entropy const entropy_;
    one_byte const sign_;
    hash_digest const data_;
};

} // namespace kth::domain::wallet

#endif
