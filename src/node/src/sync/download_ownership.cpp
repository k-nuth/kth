// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/download_ownership.hpp>

#include <algorithm>

namespace kth::node::sync {

void download_ownership::set_known(std::span<uint64_t const> nonces) {
    // A SNAPSHOT, not an addition: whatever is absent has been withdrawn. The
    // live slots are deliberately left alone — a worker on its way out still
    // owns its slot and must still be able to release it, it just may not be
    // started again.
    known_.clear();
    known_.insert(nonces.begin(), nonces.end());
}

std::vector<uint64_t> download_ownership::begin_range() {
    ++epoch_;

    std::vector<uint64_t> to_start;
    to_start.reserve(known_.size());
    for (auto const nonce : known_) {
        if ( ! slots_.contains(nonce)) {
            to_start.push_back(nonce);
        }
    }
    // Sorted so the caller starts them in a stable order and a test can assert
    // on the sequence rather than on whatever the hash container yields.
    std::ranges::sort(to_start);
    return to_start;
}

void download_ownership::record(uint64_t nonce, uint64_t task_id, uint64_t epoch) {
    slots_[nonce] = download_slot{task_id, epoch};
}

std::optional<uint64_t> download_ownership::ended(
    uint64_t nonce, uint64_t task_id, uint64_t epoch, bool coordinator_wants_workers) {

    auto const it = slots_.find(nonce);
    if (it == slots_.end()) {
        return std::nullopt;   // already released: a duplicate or a retried send
    }

    // RULE 1. The live slot decides. A report that does not name it belongs to a
    // worker that was already retired, and retiring the current one on its
    // strength is the ABA this pair of fields exists to prevent — the peer would
    // be left with no consumer and no event coming.
    if (it->second.task_id != task_id || it->second.epoch != epoch) {
        return std::nullopt;
    }

    slots_.erase(it);

    // RULE 3. A worker of the CURRENT range: release and nothing more. The
    // message carries no reason, so completion, a stop, a peer failure and a
    // transient error arrive identically, and restarting on all four is a spin.
    if (epoch == epoch_) {
        return std::nullopt;
    }

    // RULE 2. A worker of an earlier range has just freed the only slot its peer
    // has. This is the moment the peer can be handed to the current coordinator,
    // and doing it here is what removes the dependency on an unrelated peer
    // event — which is the whole of #652.
    if ( ! coordinator_wants_workers) {
        return std::nullopt;
    }
    if ( ! known_.contains(nonce)) {
        return std::nullopt;   // withdrawn while its worker was leaving
    }
    return nonce;
}

std::optional<download_slot> download_ownership::slot_of(uint64_t nonce) const {
    auto const it = slots_.find(nonce);
    if (it == slots_.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace kth::node::sync
