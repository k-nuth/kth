// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_USER_AGENT_HPP_
#define KTH_NODE_USER_AGENT_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace kth::node {

// Format excessive block size as "EBxx.x" (e.g., "EB32.0" for 32MB)
std::string format_eb(uint64_t block_size_bytes);

// Build user agent string with features list
// Format: /ClientName:Version(feature1; feature2; ...)/
// Example: /Knuth:0.75.0(EB32.0)/
std::string get_user_agent(std::vector<std::string> const& features);

} // namespace kth::node

#endif //KTH_NODE_USER_AGENT_HPP_
