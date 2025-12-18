// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/user_agent.hpp>

#include <format>

#include <kth/domain/version.hpp>

namespace kth::node {

std::string get_user_agent() {
    // Format: /ClientName:Version(Currency)/
    // Example: /Knuth:0.74.0(BCH)/
    return std::format("/{}:{}({})/", kth::client_name, kth::version, kth::currency);
}

} // namespace kth::node
