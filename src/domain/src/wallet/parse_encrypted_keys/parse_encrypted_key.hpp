// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_PARSE_ENCRYPTED_KEY_HPP
#define KTH_PARSE_ENCRYPTED_KEY_HPP

#include <cstddef>
#include <cstdint>

#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "parse_encrypted_prefix.hpp"

namespace kth::domain::wallet {

// Swap not defined.
template <size_t PrefixSize>
class parse_encrypted_key
    : public parse_encrypted_prefix<PrefixSize> {
public:
    parse_encrypted_key(byte_array<PrefixSize> const& prefix,
                        one_byte const& flags,
                        ek_salt const& salt,
                        ek_entropy const& entropy);

    bool compressed() const;
    bool lot_sequence() const;
    data_chunk owner_salt() const;

    uint8_t flags() const;
    ek_salt salt() const;
    ek_entropy entropy() const;

private:
    one_byte const flags_;
    ek_salt const salt_;
    ek_entropy const entropy_;
};

} // namespace kth::domain::wallet

#include "parse_encrypted_key.ipp"

#endif
