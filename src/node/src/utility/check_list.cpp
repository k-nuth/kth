// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/utility/check_list.hpp>

#include <cstddef>
#include <utility>
#include <boost/bimap/support/lambda.hpp>
#include <kth/blockchain.hpp>

namespace kth::node {

using namespace kth::database;

bool check_list::empty() const {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    shared_lock lock(mutex_);

    return checks_.empty();
    ///////////////////////////////////////////////////////////////////////////
}

size_t check_list::size() const {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    shared_lock lock(mutex_);

    return checks_.size();
    ///////////////////////////////////////////////////////////////////////////
}

void check_list::reserve(heights const& heights) {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    unique_lock lock(mutex_);

    checks_.clear();

    for (auto const height : heights) {
        auto const it = checks_.insert({ null_hash, height });
    }

    ///////////////////////////////////////////////////////////////////////////
}


void check_list::enqueue(hash_digest&& hash, size_t height) {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    unique_lock lock(mutex_);

    using namespace boost::bimaps;
    auto const it = checks_.right.find(height);

    // Ignore the entry if it is not reserved.
    if (it == checks_.right.end()) {
        return;
    }

    KTH_ASSERT(it->second == null_hash);
    checks_.right.modify_data(it, _data = std::move(hash));
    ///////////////////////////////////////////////////////////////////////////
}

bool check_list::dequeue(hash_digest& out_hash, size_t& out_height) {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    unique_lock lock(mutex_);

    // Overlocking to reduce code in the dominant path.
    if (checks_.empty()) {
        return false;
    }

    auto it = checks_.right.begin();
    out_height = it->first;
    out_hash = it->second;
    checks_.right.erase(it);
    return true;
    ///////////////////////////////////////////////////////////////////////////
}

} // namespace kth::node
