// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_NOISE_CREDENTIALS_HPP
#define KTH_NODE_SV2_NOISE_CREDENTIALS_HPP

#include <cstdint>
#include <optional>
#include <string>

#include <kth/node/settings.hpp>
#include <kth/node/sv2/noise.hpp>

namespace kth::node::sv2 {

struct noise_credentials {
    noise_keypair static_key;
    noise_certificate certificate;
    hash_digest authority_public_key;
    // Present when the authority secret is online. Each handshake then gets a
    // fresh certificate, so a provider that runs longer than one certificate
    // lifetime never serves an expired one.
    std::optional<noise_keypair> authority;
    uint32_t certificate_validity_seconds = 0;

    // The certificate for a handshake at `now`, or nullopt when an offline
    // certificate is outside its validity period.
    [[nodiscard]] std::optional<noise_certificate> certificate_at(uint32_t now) const;
};

// Load and authenticate everything before binding the listener. Secret files
// are created only when absent; an invalid existing key is never replaced.
[[nodiscard]]
std::optional<noise_credentials> load_noise_credentials(sv2_settings const& settings,
    uint32_t now, std::string& error);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_NOISE_CREDENTIALS_HPP
