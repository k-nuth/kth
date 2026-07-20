// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_TEST_HELPERS_HPP
#define KTH_DOMAIN_TEST_HELPERS_HPP

#include <string>

// Include infrastructure helpers (domain depends on infrastructure)
#include "../../infrastructure/test/test_helpers.hpp"

// Include domain types for full support
#include <kth/domain.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/network_address.hpp>

// Pretty printing for domain-specific types via Catch2's StringMaker
// customization point. All specializations print via
// `kth::to_data_chunk(x, args...)` so they stay in sync with the
// byte_writer-based serialization API.
namespace Catch {

template <>
struct StringMaker<kth::infrastructure::message::network_address> {
    static
    std::string convert(kth::infrastructure::message::network_address const& x) {
        return kth::encode_base16(kth::to_data_chunk(x, kth::domain::message::version::level::minimum, false));
    }
};

template <>
struct StringMaker<kth::domain::chain::input> {
    static
    std::string convert(kth::domain::chain::input const& x) {
        return kth::encode_base16(kth::to_data_chunk(x, true));
    }
};

template <>
struct StringMaker<kth::domain::chain::output> {
    static
    std::string convert(kth::domain::chain::output const& x) {
        return kth::encode_base16(kth::to_data_chunk(x, true));
    }
};

template <>
struct StringMaker<kth::domain::chain::transaction> {
    static
    std::string convert(kth::domain::chain::transaction const& x) {
        return kth::encode_base16(kth::to_data_chunk(x, true));
    }
};

template <>
struct StringMaker<kth::domain::message::prefilled_transaction> {
    static
    std::string convert(kth::domain::message::prefilled_transaction const& x) {
        return kth::encode_base16(kth::to_data_chunk(x, kth::domain::message::version::level::minimum));
    }
};

} // namespace Catch

#endif // KTH_DOMAIN_TEST_HELPERS_HPP
