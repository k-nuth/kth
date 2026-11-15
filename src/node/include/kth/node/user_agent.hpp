// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_USER_AGENT_HPP_
#define KTH_NODE_USER_AGENT_HPP_

#include <string>
#include <vector>

namespace kth::node {

// Build user agent string with features list
// Format: /ClientName:Version(feature1; feature2; ...)/
// Example: /Knuth:1.6.0(example; other)/
std::string get_user_agent(std::vector<std::string> const& features);

} // namespace kth::node

#endif //KTH_NODE_USER_AGENT_HPP_
