// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_PARSE_ENCRYPTED_PREFIX_IPP
#define KTH_PARSE_ENCRYPTED_PREFIX_IPP

#include <cstddef>
#include <cstdint>

#include <kth/domain/wallet/encrypted_keys.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::wallet {

template <size_t Size>
parse_encrypted_prefix<Size>::parse_encrypted_prefix(
    byte_array<Size> const& value)
    : prefix_(value), valid_(false) {
}

template <size_t Size>
uint8_t parse_encrypted_prefix<Size>::context() const {
    return prefix_.back();
}

template <size_t Size>
byte_array<Size> parse_encrypted_prefix<Size>::prefix() const {
    return prefix_;
}

template <size_t Size>
bool parse_encrypted_prefix<Size>::valid() const {
    return valid_;
}

template <size_t Size>
void parse_encrypted_prefix<Size>::valid(bool value) {
    valid_ = value;
}

} // namespace kth::domain::wallet

#endif
