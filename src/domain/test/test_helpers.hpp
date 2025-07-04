// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_TEST_HELPERS_HPP
#define KTH_DOMAIN_TEST_HELPERS_HPP

#include <ostream>

// Include infrastructure helpers (domain depends on infrastructure)
#include "../../infrastructure/test/test_helpers.hpp"

// Include domain types for full support
#include <kth/domain.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/infrastructure/message/network_address.hpp>

// Pretty printing operators for domain-specific types
namespace kth::infrastructure::message {

inline
std::ostream& operator<<(std::ostream& os, network_address const& x) {
    os << encode_base16(x.to_data(kth::domain::message::version::level::minimum, false));
    return os;
}

} // namespace kth::infrastructure::message

namespace kth::domain::chain {

inline
std::ostream& operator<<(std::ostream& os, input const& x) {
    os << encode_base16(x.to_data());
    return os;
}

inline
std::ostream& operator<<(std::ostream& os, output const& x) {
    os << encode_base16(x.to_data());
    return os;
}

inline
std::ostream& operator<<(std::ostream& os, transaction const& x) {
    os << encode_base16(x.to_data());
    return os;
}

} // namespace kth::domain::chain

namespace kth::domain::message {

inline
std::ostream& operator<<(std::ostream& os, prefilled_transaction const& x) {
    os << encode_base16(x.to_data(version::level::minimum));
    return os;
}

} // namespace kth::domain::message


#endif // KTH_DOMAIN_TEST_HELPERS_HPP
