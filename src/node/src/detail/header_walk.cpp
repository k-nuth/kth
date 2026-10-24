// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/header_walk.hpp>

#include <algorithm>
#include <utility>

namespace kth::node::detail {

walk_step header_walk::restart(uint32_t tip) {
    begin_walk();
    return advance(tip);
}

walk_step header_walk::peers_changed(std::vector<walk_peer> peers, uint32_t tip) {
    peers_ = std::move(peers);

    // A peer that left takes its history with it: a session that comes back is
    // a new session, with a new nonce, and nothing it said before applies.
    erase_if(spent_, [&](uint64_t nonce) { return ! known(nonce); });
    if (preferred_ && ! known(*preferred_)) {
        preferred_.reset();
    }

    // Gone peers answer nothing, so waiting out their deadline would only delay
    // the next one. Its deadline, when it fires, finds nothing to end.
    if (active_ && ! known(active_->peer)) {
        end_active();
    }

    // A peer claiming more than we hold is a reason to ask, even with the tip
    // confirmed. Without it a node below every peer would wait for an
    // announcement to learn what the version handshake already told it.
    if ( ! walking_ && ! owed_ && best_known_height() > tip) {
        begin_walk();
    }

    return advance(tip);
}

walk_step header_walk::progress(uint64_t source, uint32_t tip) {
    // Adopted from anyone. The chain moved, so every "nothing new" heard so far
    // was about a tip we no longer have, and the peer that moved it is the one
    // most likely to have more.
    answered();
    begin_walk();
    preferred_ = source;

    if (active_ && active_->peer == source) {
        end_active();
    }
    // A different peer's stimulus stays out: it will end on its own, and the
    // walk carries on from the tip as it stands then.
    return advance(tip);
}

walk_step header_walk::progress_then_failure(uint64_t source, uint32_t tip) {
    // The tip moved, whoever moved it and however the batch ended: a new walk.
    answered();
    begin_walk();
    // Not preferred — what it had past the prefix was rejected — and spent for
    // this walk, so the next stimulus goes to someone else.
    if (preferred_ == source) {
        preferred_.reset();
    }
    spent_.insert(source);

    if (active_ && active_->peer == source) {
        end_active();
    }
    return advance(tip);
}

walk_step header_walk::no_progress(uint64_t source, uint32_t tip) {
    // Only the peer being asked can answer the question being asked. From
    // anyone else this is a late answer to an old question, or an announcement
    // of something we hold: neither says anything about this walk.
    if ( ! active_ || active_->peer != source) {
        return {};
    }
    answered();
    spent_.insert(source);
    // Evidence only if what it answers was asked after the last debt. An
    // answer to an earlier stimulus is about the chain as it was before
    // whatever the debt is about, however late it arrives.
    if (active_->seq > owed_after_) {
        evidence_ = true;
    }
    end_active();
    return advance(tip);
}

walk_step header_walk::failure(uint64_t source, uint32_t tip) {
    if ( ! active_ || active_->peer != source) {
        return {};
    }
    answered();
    // Spent, but not evidence: a rejected answer says nothing about whether
    // the tip is current.
    spent_.insert(source);
    end_active();
    return advance(tip);
}

walk_step header_walk::timed_out(uint64_t seq, uint32_t tip) {
    if ( ! active_ || active_->seq != seq) {
        return {};      // already ended by a negative, a send failure or a departure
    }
    spent_.insert(active_->peer);
    end_active();
    return advance(tip);
}

walk_step header_walk::send_failed(uint64_t seq, uint32_t tip) {
    // The same ending as a deadline, for the same reason: this peer will not be
    // answering this stimulus.
    return timed_out(seq, tip);
}

void header_walk::delivered(uint64_t seq) {
    if (owed_ && seq > owed_after_) {
        owed_ = false;
    }
}

walk_step header_walk::retry_due(uint64_t generation, uint32_t tip) {
    if ( ! parked_ || generation != park_generation_) {
        return {};      // moved on since: a peer, a header or a debt did it
    }
    // Bounded by the caller's backoff, which grows with each retry nobody
    // answers. Kept finite here only so the count cannot wrap.
    if (retry_attempt_ < 32) {
        ++retry_attempt_;
    }
    // Everyone is asked again from the tip: a peer that timed out may have been
    // slow, or its answer lost, and nothing about it says it cannot answer now.
    begin_walk();
    return advance(tip);
}

walk_step header_walk::owe(uint32_t tip) {
    // Everything issued so far was issued before this, so none of it can be
    // relied on to bring what this is about — and nothing heard in answer to
    // it says the tip is current any more.
    owed_ = true;
    owed_after_ = last_seq_;
    debt_walk_begun_ = false;
    evidence_ = false;
    return advance(tip);
}

uint32_t header_walk::best_known_height() const {
    uint32_t best = 0;
    for (auto const& peer : peers_) {
        best = std::max(best, peer.claimed_height);
    }
    return best;
}

size_t header_walk::eligible_count() const {
    return size_t(std::count_if(peers_.begin(), peers_.end(),
        [&](walk_peer const& peer) { return eligible(peer.nonce); }));
}

bool header_walk::known(uint64_t nonce) const {
    return std::any_of(peers_.begin(), peers_.end(),
        [&](walk_peer const& peer) { return peer.nonce == nonce; });
}

bool header_walk::eligible(uint64_t nonce) const {
    return known(nonce) && ! spent_.contains(nonce);
}

void header_walk::answered() {
    retry_attempt_ = 0;
}

void header_walk::begin_walk() {
    walking_ = true;
    spent_.clear();
    evidence_ = false;
}

void header_walk::end_active() {
    active_.reset();
}

walk_step header_walk::advance(uint32_t tip) {
    if (active_) {
        return {};      // one stimulus at a time
    }
    // Whatever this decides, the walk is no longer in the park it was in.
    parked_ = false;

    // Settled with a debt: the debt is a walk of its own, from scratch.
    if ( ! walking_ && owed_) {
        begin_walk();
        debt_walk_begun_ = true;
    }
    if ( ! walking_) {
        return {};
    }

    auto pick = [&]() -> std::optional<uint64_t> {
        if (preferred_ && eligible(*preferred_)) {
            return preferred_;
        }
        for (auto const& peer : peers_) {
            if (eligible(peer.nonce)) {
                return peer.nonce;
            }
        }
        return std::nullopt;
    };

    auto next = pick();
    if ( ! next && owed_ && ! debt_walk_begun_) {
        // Everyone was spent before the debt existed, so none of them has been
        // asked about it. Once per debt: if this walk also runs dry without a
        // delivery, the debt waits for a peer set that can take it.
        begin_walk();
        debt_walk_begun_ = true;
        next = pick();
    }

    if (next) {
        active_ = walk_stimulus{*next, ++last_seq_};
        return walk_step{.confirm_tip = false, .stimulate = active_};
    }

    // Confirmed on what the peers that were asked answered, and on nothing
    // they merely claimed. Every peer has had its turn from this tip by now,
    // so a claim above it has had its chance to be backed by headers; the claim
    // itself comes from the version handshake and nothing verifies it. Were it
    // a veto, one peer claiming a height nobody has and answering nothing
    // would hold the tip unconfirmed for as long as it stayed connected.
    if ( ! owed_ && evidence_) {
        walking_ = false;
        return walk_step{.confirm_tip = true, .stimulate = std::nullopt};
    }

    // Parked: nobody left to ask and the answer is not in. A peer set change,
    // progress or a new debt moves it — and with peers connected, so does a
    // retry, because none of those may ever come: every peer can simply have
    // answered nothing in time. With no peers there is nobody to retry, and a
    // peer arriving is the event.
    parked_ = true;
    ++park_generation_;
    if (peers_.empty()) {
        return {};
    }
    return walk_step{
        .confirm_tip = false,
        .stimulate = std::nullopt,
        .retry = walk_retry{park_generation_, retry_attempt_}};
}

} // namespace kth::node::detail
