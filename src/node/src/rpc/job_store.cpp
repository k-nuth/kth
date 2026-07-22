// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/job_store.hpp>

#include <utility>

#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/timer.hpp>

namespace kth::node::rpc {

job_store::job_store(std::size_t max_jobs, std::uint32_t ttl_seconds)
    : max_jobs_(max_jobs == 0 ? 1 : max_jobs)
    , ttl_seconds_(ttl_seconds)
{}

std::string job_store::add(std::vector<transaction_const_ptr> txs) {
    // Content-derived id: double-SHA256 over the concatenated txids.
    data_chunk material;
    material.reserve(txs.size() * hash_size);
    for (auto const& tx : txs) {
        auto const h = tx->hash();
        material.insert(material.end(), h.begin(), h.end());
    }
    auto const job_id = encode_base16(bitcoin_hash(material));

    std::lock_guard<std::mutex> lock(mutex_);
    auto const now = static_cast<std::uint32_t>(zulu_time());
    auto const [it, inserted] = jobs_.try_emplace(job_id, entry{std::move(txs), now});
    if (inserted) {
        order_.push_back(job_id);
    } else {
        it->second.created = now; // refresh TTL on re-add
    }
    evict_locked();
    return job_id;
}

std::optional<std::vector<transaction_const_ptr>>
job_store::get(std::string const& job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto const it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        return std::nullopt;
    }
    auto const now = static_cast<std::uint32_t>(zulu_time());
    if (now - it->second.created > ttl_seconds_) {
        return std::nullopt;
    }
    return it->second.txs;
}

// Requires mutex_ held. Drops expired jobs and enforces the count bound.
void job_store::evict_locked() {
    auto const now = static_cast<std::uint32_t>(zulu_time());
    while ( ! order_.empty()) {
        auto const& oldest = order_.front();
        auto const it = jobs_.find(oldest);
        bool const expired = it != jobs_.end() &&
            now - it->second.created > ttl_seconds_;
        if (jobs_.size() <= max_jobs_ && ! expired && it != jobs_.end()) {
            break;
        }
        if (it != jobs_.end()) {
            jobs_.erase(it);
        }
        order_.pop_front();
    }
}

} // namespace kth::node::rpc
