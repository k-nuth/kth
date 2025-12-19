// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_SALTED_HASHERS_HPP
#define KTH_INFRASTRUCTURE_SALTED_HASHERS_HPP

#include <array>
#include <cstdint>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth {

// =============================================================================
// Salted IP Address Hasher (BCHN-style)
// =============================================================================
// Uses SipHash-2-4 with random salt for hash table resistance against
// collision attacks. Salt is generated once per hasher instance.

struct KI_API salted_ip_hasher {
    salted_ip_hasher() noexcept {
        std::array<uint64_t, 2> salt{};
        pseudo_random::fill(salt);
        k0_ = salt[0];
        k1_ = salt[1];
    }

    size_t operator()(::asio::ip::address const& addr) const noexcept {
        sip_hasher hasher(k0_, k1_);
        if (addr.is_v4()) {
            auto const bytes = addr.to_v4().to_bytes();
            hasher.write(bytes.data(), bytes.size());
        } else {
            auto const bytes = addr.to_v6().to_bytes();
            hasher.write(bytes.data(), bytes.size());
        }
        return size_t(hasher.finalize());
    }

private:
    uint64_t k0_;
    uint64_t k1_;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_SALTED_HASHERS_HPP
