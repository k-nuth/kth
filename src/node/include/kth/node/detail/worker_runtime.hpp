// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_WORKER_RUNTIME_HPP
#define KTH_NODE_DETAIL_WORKER_RUNTIME_HPP

#if defined(KTH_BROWSER_NODE)

#include <memory>

#include <kth/infrastructure/utility/browser_runtime.hpp>

namespace kth::node::detail {

// The Worker's one runtime. The application's, when it runs one (not owned
// here); otherwise one shared by this Worker's header clients and wallet
// support, created by the first and kept while any of them lives.
inline std::shared_ptr<browser::runtime> worker_runtime() {
    static std::weak_ptr<browser::runtime> shared;
    if (auto existing = shared.lock()) return existing;
    if (auto* application = browser::runtime::find()) {
        return std::shared_ptr<browser::runtime>(application, [](browser::runtime*) {});
    }
    auto created = std::make_shared<browser::runtime>();
    shared = created;
    return created;
}

} // namespace kth::node::detail

#endif // KTH_BROWSER_NODE

#endif // KTH_NODE_DETAIL_WORKER_RUNTIME_HPP
