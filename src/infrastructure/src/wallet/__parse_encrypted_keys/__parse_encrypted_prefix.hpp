// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CRYPTO_PARSE_ENCRYPTED_PREFIX_HPP
#define KTH_CRYPTO_PARSE_ENCRYPTED_PREFIX_HPP

#include <cstdint>
#include <cstddef>

#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/wallet/encrypted_keys.hpp>

// BIP38
// Alt-chain implementers should exploit the address hash for [identification].
// Since each operation in this proposal involves hashing a text representation
// of a coin address which (for Bitcoin) includes the leading '1', an alt-chain
// can easily be denoted simply by using the alt-chain's preferred format for
// representing an address.... Alt-chain implementers may also change the prefix
// such that encrypted addresses do not start with "6P".

namespace kth::infrastructure::wallet {

template <size_t Size>
class parse_encrypted_prefix
{
public:
    bool valid() const;

    static constexpr uint8_t prefix_size = Size;

protected:
    explicit parse_encrypted_prefix(byte_array<Size> const& value);

    uint8_t context() const;
    byte_array<Size> prefix() const;
    void valid(bool value);

    static constexpr uint8_t magic_size = Size - 1;

private:
    bool verify_magic() const;

    const byte_array<Size> prefix_;
    bool valid_;
};

} // namespace kth::infrastructure::wallet

#include "parse_encrypted_prefix.ipp"

#endif
