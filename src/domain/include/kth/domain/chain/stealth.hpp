// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_STEALTH_HPP
#define KTH_DOMAIN_CHAIN_STEALTH_HPP

#include <cstdint>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::domain::chain {

/// This structure is used in the client-server protocol in v2/v3.
/// The stealth row excludes the sign byte (0x02) of the ephemeral public key.
struct KD_API stealth_compact {
    using list = std::vector<stealth_compact>;

    hash_digest ephemeral_public_key_hash;
    short_hash public_key_hash;
    hash_digest transaction_hash;

    friend
    auto operator<=>(stealth_compact const&, stealth_compact const&) = default;
};

/// This structure is used between client and API callers in v2/v3.
/// The normal stealth row includes the sign byte of the ephemeral public key.
struct KD_API stealth {
    using list = std::vector<stealth>;

    ec_compressed ephemeral_public_key;
    short_hash public_key_hash;
    hash_digest transaction_hash;

    friend
    auto operator<=>(stealth const&, stealth const&) = default;
};

} // namespace kth::domain::chain

#endif
