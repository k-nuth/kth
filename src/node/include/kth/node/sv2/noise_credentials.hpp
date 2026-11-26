// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_NOISE_CREDENTIALS_HPP
#define KTH_NODE_SV2_NOISE_CREDENTIALS_HPP

#include <kth/node/sv2/feature.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include <kth/node/settings.hpp>
#include <kth/node/sv2/noise.hpp>

namespace kth::node::sv2 {

// The Template Provider's Noise identity: its static key, the authority that
// vouches for it and the certificate handshakes show.
struct noise_credentials {
    noise_keypair static_key;
    hash_digest authority_public_key;
    // The authority's secret key when it is online: the node signs its own
    // certificates. Absent with a certificate signed offline.
    std::optional<noise_keypair> authority;
    uint32_t certificate_validity_seconds = 0;
    // The certificate handshakes show. Online, signed at start-up and signed
    // again once less than half of its lifetime is left; offline, the one in
    // sv2.certificate_file.
    noise_certificate certificate;

    // The certificate for a handshake at `now`: an online one is signed again
    // here when it has to be, so every handshake gets one with at least half
    // a lifetime ahead without signing in every handshake. An offline one
    // outside its validity window is refused.
    [[nodiscard]] std::expected<noise_certificate, noise_error> certificate_at(uint32_t now);
};

// The clock as certificates carry it: U32 seconds (sv2-spec 4.5.2).
[[nodiscard]] std::expected<uint32_t, noise_error> noise_clock();

// Key and certificate paths that are empty or relative are taken from the
// data directory: the static and authority keys default to sv2_static_key and
// sv2_authority_key there. The node's Noise identity then goes where its chain
// goes, whatever directory it is started from.
[[nodiscard]] sv2_settings with_noise_files(sv2_settings settings,
    std::filesystem::path const& data_directory);

// Loads and checks everything before the listener is bound, so that a node
// with credentials it cannot use does not start. Key files are created only
// when absent, and never replaced. With sv2.certificate_file, the static key
// is created only while there is no certificate yet, and the error then
// names the static public key the operator signs.
[[nodiscard]]
std::expected<noise_credentials, std::string> load_noise_credentials(sv2_settings const& settings,
    uint32_t now);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_NOISE_CREDENTIALS_HPP
