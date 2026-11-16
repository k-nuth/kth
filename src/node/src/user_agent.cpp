// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/user_agent.hpp>

#include <format>

#include <kth/domain/version.hpp>

namespace kth::node {

std::string get_user_agent(std::vector<std::string> const& features) {
    // Format: /ClientName:Version(feature1; feature2; ...)/
    // Example: /Knuth:1.6.0(example; other)/
    if (features.empty()) {
        return std::format("/{}:{}/", kth::client_name, kth::version);
    }

    std::string features_str;
    for (size_t i = 0; i < features.size(); ++i) {
        if (i > 0) {
            features_str += "; ";
        }
        features_str += features[i];
    }

    return std::format("/{}:{}({})/", kth::client_name, kth::version, features_str);
}

} // namespace kth::node
