// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <vector>

#include <kth/node/detail/header_walk.hpp>

using namespace kth::node::detail;

// =============================================================================
// The header walk, as state (#710)
// =============================================================================
//
// A `headers` is a fact, not an answer, so the walk is driven by what the
// organizer says about headers and by WHO sent them — never by matching them to
// a request. These controls drive every input the coordinator feeds it and read
// back what it decided: whom to stimulate, with which sequence number, and when
// the tip is confirmed.

namespace {

constexpr uint64_t p = 1;
constexpr uint64_t q = 2;
constexpr uint64_t r = 3;

std::vector<walk_peer> peers_at(uint32_t height, std::vector<uint64_t> const& nonces) {
    std::vector<walk_peer> out;
    for (auto nonce : nonces) {
        out.push_back(walk_peer{nonce, height});
    }
    return out;
}

// A walk with peers P and Q claiming `height`, already restarted at `tip`, so
// the first stimulus is out to P.
header_walk walking(uint32_t tip, uint32_t height) {
    header_walk walk;
    auto const started = walk.restart(tip);
    REQUIRE_FALSE(started.stimulate);       // nobody to ask yet
    auto const step = walk.peers_changed(peers_at(height, {p, q}), tip);
    REQUIRE(step.stimulate);
    REQUIRE(step.stimulate->peer == p);
    return walk;
}

} // namespace

// -----------------------------------------------------------------------------
// The walk
// -----------------------------------------------------------------------------

TEST_CASE("header walk: one stimulus at a time, each peer asked once per walk",
          "[header_walk]") {
    auto walk = walking(100, 100);
    auto const first = *walk.active();

    // P has nothing past the tip: Q is next, with a new sequence number.
    auto const after_p = walk.no_progress(p, 100);
    REQUIRE(after_p.stimulate);
    CHECK(after_p.stimulate->peer == q);
    CHECK(after_p.stimulate->seq > first.seq);
    CHECK_FALSE(after_p.confirm_tip);
    CHECK(walk.spent(p));

    // And Q neither: nobody is left, both answered while asked, and the tip is
    // at the height they claim. Confirmed, without another stimulus.
    auto const after_q = walk.no_progress(q, 100);
    CHECK(after_q.confirm_tip);
    CHECK_FALSE(after_q.stimulate);
    CHECK_FALSE(walk.walking());
}

TEST_CASE("header walk: the tip is confirmed once per walk", "[header_walk]") {
    auto walk = walking(100, 100);
    auto const seq_p = walk.active()->seq;
    REQUIRE(walk.no_progress(p, 100).stimulate);
    auto const seq_q = walk.active()->seq;
    REQUIRE(walk.no_progress(q, 100).confirm_tip);

    // What arrives afterwards — late deadlines, late negatives, a peer set
    // that did not change who is there — confirms nothing again.
    CHECK_FALSE(walk.timed_out(seq_p, 100).confirm_tip);
    CHECK_FALSE(walk.timed_out(seq_q, 100).confirm_tip);
    CHECK_FALSE(walk.no_progress(p, 100).confirm_tip);
    auto const same_peers = walk.peers_changed(peers_at(100, {p, q}), 100);
    CHECK_FALSE(same_peers.confirm_tip);
    CHECK_FALSE(same_peers.stimulate);
}

TEST_CASE("header walk: a negative from a peer that is not being asked changes nothing",
          "[header_walk]") {
    auto walk = walking(100, 100);
    auto const active = *walk.active();

    // Q's empty or stale `headers` may be a late answer to something older,
    // or an announcement of a block we hold. It is not an answer to the
    // stimulus out to P.
    auto const step = walk.no_progress(q, 100);
    CHECK_FALSE(step.stimulate);
    CHECK_FALSE(step.confirm_tip);
    REQUIRE(walk.active());
    CHECK(*walk.active() == active);
    CHECK_FALSE(walk.spent(q));

    // Faithful negation: the same message from P does end it.
    auto const from_p = walk.no_progress(p, 100);
    REQUIRE(from_p.stimulate);
    CHECK(from_p.stimulate->peer == q);
}

TEST_CASE("header walk: a late reception is not attributed to the next stimulus",
          "[header_walk]") {
    // P is asked and its deadline expires; Q is asked. Then P's empty answer to
    // the stimulus that expired arrives, and then a second one. Neither is Q's
    // answer, and neither ends Q's stimulus.
    auto walk = walking(100, 100);
    auto const seq_p = walk.active()->seq;

    auto const timed_out = walk.timed_out(seq_p, 100);
    REQUIRE(timed_out.stimulate);
    REQUIRE(timed_out.stimulate->peer == q);
    auto const q_stimulus = *timed_out.stimulate;

    for (int i = 0; i < 2; ++i) {
        auto const late = walk.no_progress(p, 100);
        CHECK_FALSE(late.stimulate);
        CHECK_FALSE(late.confirm_tip);
        REQUIRE(walk.active());
        CHECK(*walk.active() == q_stimulus);
    }

    // A timeout is not evidence, and P's late negatives were not taken as any:
    // with Q also timing out the walk parks instead of confirming.
    auto const q_out = walk.timed_out(q_stimulus.seq, 100);
    CHECK_FALSE(q_out.confirm_tip);
    CHECK_FALSE(q_out.stimulate);
    CHECK(walk.walking());
    // Parked — and not for good: a retry is scheduled. See "every peer timing
    // out" below.
    CHECK(q_out.retry.has_value());
}

TEST_CASE("header walk: the active peer's negative and its deadline end it once",
          "[header_walk]") {
    auto walk = walking(100, 100);
    auto const seq_p = walk.active()->seq;

    // The negative first: Q is asked.
    auto const negative = walk.no_progress(p, 100);
    REQUIRE(negative.stimulate);
    auto const q_stimulus = *negative.stimulate;

    // Then P's deadline, which was armed for the same stimulus. It finds
    // nothing to end — in particular not Q's.
    auto const deadline = walk.timed_out(seq_p, 100);
    CHECK_FALSE(deadline.stimulate);
    CHECK_FALSE(deadline.confirm_tip);
    REQUIRE(walk.active());
    CHECK(*walk.active() == q_stimulus);

    // The other order: the deadline first, then the negative from the peer
    // whose stimulus it ended. Also once.
    auto const q_deadline = walk.timed_out(q_stimulus.seq, 100);
    CHECK_FALSE(q_deadline.stimulate);       // both spent, P with evidence
    CHECK(q_deadline.confirm_tip);
    auto const late = walk.no_progress(q, 100);
    CHECK_FALSE(late.stimulate);
    CHECK_FALSE(late.confirm_tip);
}

TEST_CASE("header walk: a deadline for the current stimulus ends it, a stale one does not",
          "[header_walk]") {
    auto walk = walking(100, 100);
    auto const seq_p = walk.active()->seq;

    // Stale: a number the walk never handed out, and one from before.
    CHECK_FALSE(walk.timed_out(seq_p + 7, 100).stimulate);
    CHECK_FALSE(walk.timed_out(0, 100).stimulate);
    REQUIRE(walk.active());
    CHECK(walk.active()->seq == seq_p);

    // Current.
    auto const current = walk.timed_out(seq_p, 100);
    REQUIRE(current.stimulate);
    CHECK(current.stimulate->peer == q);
    CHECK(walk.spent(p));
}

TEST_CASE("header walk: a failed send ends the stimulus like a deadline",
          "[header_walk]") {
    auto walk = walking(100, 100);
    auto const seq_p = walk.active()->seq;

    auto const failed = walk.send_failed(seq_p, 100);
    REQUIRE(failed.stimulate);
    CHECK(failed.stimulate->peer == q);
    // And the deadline that follows it finds nothing.
    CHECK_FALSE(walk.timed_out(seq_p, 100).stimulate);
}

TEST_CASE("header walk: progress from any peer is adopted", "[header_walk]") {
    auto walk = walking(100, 110);
    auto const p_stimulus = *walk.active();

    // P answered empty; Q is being asked.
    auto const to_q = walk.no_progress(p, 100);
    REQUIRE(to_q.stimulate);
    REQUIRE(to_q.stimulate->peer == q);
    auto const q_stimulus = *to_q.stimulate;
    REQUIRE(walk.spent(p));

    // Now P sends headers that move the tip — unasked. Adopted: everyone is
    // eligible again. Q's stimulus is left to end on its own.
    auto const adopted = walk.progress(p, 105);
    CHECK_FALSE(adopted.stimulate);
    CHECK_FALSE(walk.spent(p));
    REQUIRE(walk.active());
    CHECK(*walk.active() == q_stimulus);

    // When Q's ends, the next stimulus goes to P — the peer that proved it has
    // more — from the new tip.
    auto const next = walk.no_progress(q, 105);
    REQUIRE(next.stimulate);
    CHECK(next.stimulate->peer == p);
    CHECK(next.stimulate->seq > q_stimulus.seq);
    CHECK(next.stimulate->seq > p_stimulus.seq);
}

TEST_CASE("header walk: progress from the active peer asks it again at once",
          "[header_walk]") {
    auto walk = walking(100, 4100);
    auto const first = *walk.active();

    auto const step = walk.progress(p, 2100);
    REQUIRE(step.stimulate);
    CHECK(step.stimulate->peer == p);
    CHECK(step.stimulate->seq > first.seq);
}

TEST_CASE("header walk: two peers progressing concurrently both count",
          "[header_walk]") {
    auto walk = walking(100, 4100);
    auto const p_first = *walk.active();

    // Q moves the tip while P is being asked; then P's own batch moves it too.
    CHECK_FALSE(walk.progress(q, 2100).stimulate);
    REQUIRE(walk.active());
    CHECK(*walk.active() == p_first);

    auto const p_progress = walk.progress(p, 4100);
    REQUIRE(p_progress.stimulate);
    CHECK(p_progress.stimulate->peer == p);

    // P now has nothing more; Q, not spent by either progress, is asked.
    auto const to_q = walk.no_progress(p, 4100);
    REQUIRE(to_q.stimulate);
    CHECK(to_q.stimulate->peer == q);

    // And with Q also done the tip, at the claimed height, is confirmed.
    CHECK(walk.no_progress(q, 4100).confirm_tip);
}

TEST_CASE("header walk: a rejected answer spends the peer but is not evidence",
          "[header_walk]") {
    auto walk = walking(100, 100);
    auto const to_q = walk.failure(p, 100);
    REQUIRE(to_q.stimulate);
    CHECK(to_q.stimulate->peer == q);
    CHECK(walk.spent(p));

    // Q fails too: nobody said "nothing past your tip", so no confirmation.
    auto const parked = walk.failure(q, 100);
    CHECK_FALSE(parked.confirm_tip);
    CHECK_FALSE(parked.stimulate);
    CHECK(walk.walking());

    // A failure from a peer that is not being asked does not end anything.
    auto again = walking(100, 100);
    auto const active = *again.active();
    CHECK_FALSE(again.failure(q, 100).stimulate);
    CHECK(*again.active() == active);
}

TEST_CASE("header walk: with nobody to ask, or nobody who answered, the tip is not confirmed",
          "[header_walk]") {
    header_walk walk;
    auto const alone = walk.restart(100);
    CHECK_FALSE(alone.confirm_tip);
    CHECK_FALSE(alone.stimulate);

    // Everyone gone after being asked and never answering.
    auto const step = walk.peers_changed(peers_at(100, {p}), 100);
    REQUIRE(step.stimulate);
    auto const gone = walk.peers_changed({}, 100);
    CHECK_FALSE(gone.confirm_tip);
    CHECK_FALSE(gone.stimulate);
    CHECK_FALSE(walk.active());
}

TEST_CASE("header walk: below the height peers claim their answers confirm, and a new peer reopens it",
          "[header_walk]") {
    // They claim 120 and we hold 100, and asked from 100 both had nothing
    // past it. Their answers are what is known; the claim is not.
    auto walk = walking(100, 120);
    REQUIRE(walk.no_progress(p, 100).stimulate);
    auto const done = walk.no_progress(q, 100);
    CHECK(done.confirm_tip);
    CHECK_FALSE(done.stimulate);
    CHECK_FALSE(walk.walking());

    // The claim is still a reason to ask: a change in who is connected, with
    // someone claiming more than we hold, starts a walk from the tip.
    auto const resumed = walk.peers_changed(peers_at(120, {p, q, r}), 100);
    REQUIRE(resumed.stimulate);
    CHECK(resumed.stimulate->peer == p);
    CHECK(walk.walking());
}

TEST_CASE("header walk: a stimulated peer that leaves ends its stimulus", "[header_walk]") {
    auto walk = walking(100, 100);
    auto const seq_p = walk.active()->seq;

    auto const step = walk.peers_changed(peers_at(100, {q}), 100);
    REQUIRE(step.stimulate);
    CHECK(step.stimulate->peer == q);
    CHECK_FALSE(walk.timed_out(seq_p, 100).stimulate);
}

TEST_CASE("header walk: a confirmed tip is provisional", "[header_walk]") {
    auto walk = walking(100, 100);
    REQUIRE(walk.no_progress(p, 100).stimulate);
    REQUIRE(walk.no_progress(q, 100).confirm_tip);

    // A new header from Q: a new walk from the new tip, Q first.
    auto const step = walk.progress(q, 101);
    REQUIRE(step.stimulate);
    CHECK(step.stimulate->peer == q);
    CHECK(walk.walking());

    REQUIRE(walk.no_progress(q, 101).stimulate);
    CHECK(walk.no_progress(p, 101).confirm_tip);
}

TEST_CASE("header walk: a peer claiming more than the tip starts a walk after confirmation",
          "[header_walk]") {
    auto walk = walking(100, 100);
    REQUIRE(walk.no_progress(p, 100).stimulate);
    REQUIRE(walk.no_progress(q, 100).confirm_tip);

    // Faithful negation first: the same peers again start nothing.
    CHECK_FALSE(walk.peers_changed(peers_at(100, {p, q}), 100).stimulate);

    std::vector<walk_peer> with_r = peers_at(100, {p, q});
    with_r.push_back(walk_peer{r, 130});
    auto const step = walk.peers_changed(with_r, 100);
    REQUIRE(step.stimulate);
}

// -----------------------------------------------------------------------------
// A height claim is a hint, not a veto
// -----------------------------------------------------------------------------
//
// `claimed_height` is the `start_height` of the peer's version message: nothing
// verifies it, and any peer — inbound included — can say anything. It is a
// reason to ask. It is not a reason to refuse what the peers that were asked
// answered: if it were, one peer claiming a height nobody has, and then
// answering nothing, would hold the tip unconfirmed for as long as it stays
// connected.

namespace {

constexpr uint32_t absurd = 4'000'000'000u;

// P claims what we hold; Q claims a height nobody has. P is being asked.
header_walk honest_and_liar(uint32_t tip) {
    header_walk walk;
    (void)walk.restart(tip);
    auto const step = walk.peers_changed({walk_peer{p, tip}, walk_peer{q, absurd}}, tip);
    REQUIRE(step.stimulate);
    REQUIRE(step.stimulate->peer == p);
    return walk;
}

} // namespace

TEST_CASE("header walk: a claim nobody backs does not veto an empty answer",
          "[header_walk][claim]") {
    // P has nothing past the tip; Q, asked next, claims far more and answers
    // empty all the same. Everyone was asked from this tip, and the answers
    // are the evidence: confirmed.
    auto walk = honest_and_liar(100);
    auto const to_q = walk.no_progress(p, 100);
    REQUIRE(to_q.stimulate);
    REQUIRE(to_q.stimulate->peer == q);

    auto const done = walk.no_progress(q, 100);
    CHECK(done.confirm_tip);
    CHECK_FALSE(done.stimulate);
    CHECK_FALSE(done.retry.has_value());     // nothing is polled on the claim's behalf
    CHECK_FALSE(walk.walking());
    CHECK_FALSE(walk.parked());
    // The claim is still there: a hint for later, not erased.
    CHECK(walk.best_known_height() == absurd);
}

TEST_CASE("header walk: a claim nobody backs does not veto a silent claimer",
          "[header_walk][claim]") {
    // The same, with Q never answering. P's answer is the evidence; Q's
    // silence is not, and neither is its claim.
    auto walk = honest_and_liar(100);
    auto const to_q = walk.no_progress(p, 100);
    REQUIRE(to_q.stimulate);

    auto const done = walk.timed_out(to_q.stimulate->seq, 100);
    CHECK(done.confirm_tip);
    CHECK_FALSE(done.stimulate);
    CHECK_FALSE(done.retry.has_value());
    CHECK_FALSE(walk.walking());
}

TEST_CASE("header walk: silence alone confirms nothing, whatever is claimed",
          "[header_walk][claim]") {
    // The guard that stays: without an answer from a peer that was asked, the
    // tip is not confirmed. Both silent, at either claim: parked, with a retry.
    auto walk = honest_and_liar(100);
    auto const to_q = walk.timed_out(walk.active()->seq, 100);
    REQUIRE(to_q.stimulate);
    auto const parked = walk.timed_out(to_q.stimulate->seq, 100);
    CHECK_FALSE(parked.confirm_tip);
    CHECK(parked.retry.has_value());
    CHECK(walk.parked());

    // And a rejected answer is not evidence either.
    auto rejected = honest_and_liar(100);
    REQUIRE(rejected.failure(p, 100).stimulate);
    auto const after = rejected.no_progress(q, 100);
    CHECK(after.confirm_tip);       // Q's empty answer is evidence; P's rejection was not
    auto none = honest_and_liar(100);
    REQUIRE(none.failure(p, 100).stimulate);
    auto const parked_again = none.failure(q, 100);
    CHECK_FALSE(parked_again.confirm_tip);
}

TEST_CASE("header walk: after confirming under a claim, events reopen the walk",
          "[header_walk][claim]") {
    // Confirmed with Q still claiming more than we hold. What reopens it is
    // what reopens any confirmed walk — headers, an announcement we cannot
    // place, a change in who is connected — and never the claim on a timer.
    auto confirmed = [] {
        auto walk = honest_and_liar(100);
        REQUIRE(walk.no_progress(p, 100).stimulate);
        REQUIRE(walk.no_progress(q, 100).confirm_tip);
        return walk;
    };

    // Q's headers, if it does have them: adopted, Q asked again first.
    auto by_progress = confirmed();
    auto const moved = by_progress.progress(q, 140);
    REQUIRE(moved.stimulate);
    CHECK(moved.stimulate->peer == q);

    // An announcement: a debt, and a fresh walk from the tip.
    auto by_debt = confirmed();
    auto const owed = by_debt.owe(100);
    REQUIRE(owed.stimulate);
    CHECK(by_debt.walking());

    // A peer set change, while someone still claims more than we hold.
    auto by_peers = confirmed();
    auto const changed = by_peers.peers_changed(
        {walk_peer{p, 100}, walk_peer{q, absurd}, walk_peer{r, 100}}, 100);
    REQUIRE(changed.stimulate);
    CHECK(by_peers.walking());
}

// -----------------------------------------------------------------------------
// Debt
// -----------------------------------------------------------------------------

TEST_CASE("header walk: a debt registered during a stimulus is followed up after it",
          "[header_walk]") {
    // P is asked; a block is announced. P's answer to the stimulus that
    // predates the announcement cannot be relied on to include it, so when P
    // answers "nothing new" the tip is NOT confirmed: a follow-up goes out.
    header_walk walk;
    (void)walk.restart(100);
    auto const first = walk.peers_changed(peers_at(100, {p}), 100);
    REQUIRE(first.stimulate);
    walk.delivered(first.stimulate->seq);

    CHECK_FALSE(walk.owe(100).stimulate);   // one stimulus at a time
    CHECK(walk.owed());

    auto const follow_up = walk.no_progress(p, 100);
    CHECK_FALSE(follow_up.confirm_tip);
    REQUIRE(follow_up.stimulate);
    CHECK(follow_up.stimulate->peer == p);
    CHECK(follow_up.stimulate->seq > first.stimulate->seq);

    // Delivered: the debt is settled, and its answer now can confirm.
    walk.delivered(follow_up.stimulate->seq);
    CHECK_FALSE(walk.owed());
    CHECK(walk.no_progress(p, 100).confirm_tip);
}

TEST_CASE("header walk: without the debt the same answers confirm the tip",
          "[header_walk]") {
    // The negation of the case above, through the same inputs minus owe().
    header_walk walk;
    (void)walk.restart(100);
    auto const first = walk.peers_changed(peers_at(100, {p}), 100);
    REQUIRE(first.stimulate);
    walk.delivered(first.stimulate->seq);

    auto const answer = walk.no_progress(p, 100);
    CHECK(answer.confirm_tip);
    CHECK_FALSE(answer.stimulate);
}

TEST_CASE("header walk: a stimulus issued before the debt does not settle it",
          "[header_walk]") {
    header_walk walk;
    (void)walk.restart(100);
    auto const first = walk.peers_changed(peers_at(100, {p}), 100);
    REQUIRE(first.stimulate);

    // Owed while the stimulus is out but before its send is reported.
    (void)walk.owe(100);
    walk.delivered(first.stimulate->seq);
    CHECK(walk.owed());
}

TEST_CASE("header walk: a debt while settled starts a fresh walk at once",
          "[header_walk]") {
    auto walk = walking(100, 100);
    REQUIRE(walk.no_progress(p, 100).stimulate);
    REQUIRE(walk.no_progress(q, 100).confirm_tip);

    auto const step = walk.owe(100);
    REQUIRE(step.stimulate);
    CHECK(walk.walking());
    CHECK_FALSE(walk.spent(p));
    CHECK_FALSE(walk.spent(q));
}

TEST_CASE("header walk: a debt with no peers waits for one", "[header_walk]") {
    header_walk walk;
    (void)walk.restart(100);
    auto const owed = walk.owe(100);
    CHECK_FALSE(owed.stimulate);
    CHECK_FALSE(owed.confirm_tip);
    CHECK(walk.owed());

    auto const step = walk.peers_changed(peers_at(100, {p}), 100);
    REQUIRE(step.stimulate);
    walk.delivered(step.stimulate->seq);
    CHECK_FALSE(walk.owed());
}

// -----------------------------------------------------------------------------
// Evidence has to come after the debt
// -----------------------------------------------------------------------------
//
// A debt says the chain may have moved since everything issued so far. An
// answer to a stimulus issued before it — heard before the debt or after — is
// about the chain as it was, so it is not evidence about the tip any more.
// Settling the debt is about asking again; confirming needs that someone asked
// after it answered.

namespace {

// P, Q and R at the tip; P is being asked, and its stimulus has left.
header_walk three_at(uint32_t tip) {
    header_walk walk;
    (void)walk.restart(tip);
    auto const step = walk.peers_changed(peers_at(tip, {p, q, r}), tip);
    REQUIRE(step.stimulate);
    REQUIRE(step.stimulate->peer == p);
    walk.delivered(step.stimulate->seq);
    return walk;
}

} // namespace

TEST_CASE("header walk: evidence from before a debt does not confirm after it",
          "[header_walk][debt][evidence]") {
    // P has nothing new: evidence. Q is asked, and while its stimulus is out a
    // block is announced. Q answers the stimulus it was sent; R is asked after
    // the debt, its stimulus leaves — which settles the debt — and R never
    // answers. Nobody asked after the debt has said anything.
    for (bool rejected : {false, true}) {
        INFO((rejected ? "R's answer is rejected" : "R does not answer"));
        auto walk = three_at(100);
        auto const to_q = walk.no_progress(p, 100);
        REQUIRE(to_q.stimulate);
        REQUIRE(to_q.stimulate->peer == q);
        walk.delivered(to_q.stimulate->seq);

        CHECK_FALSE(walk.owe(100).stimulate);       // Q is still being asked
        auto const to_r = walk.no_progress(q, 100);
        REQUIRE(to_r.stimulate);
        REQUIRE(to_r.stimulate->peer == r);
        CHECK_FALSE(to_r.confirm_tip);
        walk.delivered(to_r.stimulate->seq);
        REQUIRE_FALSE(walk.owed());                 // settled: R was asked after it

        auto const ended = rejected ? walk.failure(r, 100) : walk.timed_out(to_r.stimulate->seq, 100);
        CHECK_FALSE(ended.confirm_tip);
        CHECK_FALSE(ended.stimulate);
        REQUIRE(ended.retry.has_value());           // not abandoned: asked again later
        CHECK(walk.parked());

        // The retry asks everyone from the tip. P's answer now comes after the
        // debt, and it is enough.
        auto const again = walk.retry_due(ended.retry->generation, 100);
        REQUIRE(again.stimulate);
        CHECK(again.stimulate->peer == p);
        auto const next = walk.no_progress(p, 100);
        REQUIRE(next.stimulate);
        auto const last = walk.timed_out(next.stimulate->seq, 100);
        REQUIRE(last.stimulate);
        CHECK(walk.timed_out(last.stimulate->seq, 100).confirm_tip);
    }
}

TEST_CASE("header walk: an answer to a stimulus from before the debt is not evidence",
          "[header_walk][debt][evidence]") {
    // Nothing is heard before the debt: P never answers. Q is asked, a block is
    // announced, and only then does Q answer — the stimulus it answers was
    // issued before the debt. R, asked after it, never answers.
    auto walk = three_at(100);
    auto const to_q = walk.timed_out(walk.active()->seq, 100);
    REQUIRE(to_q.stimulate);
    REQUIRE(to_q.stimulate->peer == q);
    walk.delivered(to_q.stimulate->seq);

    (void)walk.owe(100);
    auto const to_r = walk.no_progress(q, 100);
    REQUIRE(to_r.stimulate);
    REQUIRE(to_r.stimulate->peer == r);
    walk.delivered(to_r.stimulate->seq);

    auto const ended = walk.timed_out(to_r.stimulate->seq, 100);
    CHECK_FALSE(ended.confirm_tip);
    CHECK(ended.retry.has_value());
}

TEST_CASE("header walk: with debts one after another, only the last one counts",
          "[header_walk][debt][evidence]") {
    // P is asked, a block is announced, P answers: before that debt. Q is
    // asked after it, another block is announced, Q answers: after the first
    // debt, before the second. R is asked after both. If R says nothing, none
    // of what was heard counts; if R answers, its answer is the first that
    // does.
    for (bool r_answers : {false, true}) {
        INFO((r_answers ? "R answers" : "R does not answer"));
        auto walk = three_at(100);
        (void)walk.owe(100);
        auto const to_q = walk.no_progress(p, 100);
        REQUIRE(to_q.stimulate);
        REQUIRE(to_q.stimulate->peer == q);
        walk.delivered(to_q.stimulate->seq);

        (void)walk.owe(100);
        auto const to_r = walk.no_progress(q, 100);
        REQUIRE(to_r.stimulate);
        REQUIRE(to_r.stimulate->peer == r);
        CHECK_FALSE(to_r.confirm_tip);
        walk.delivered(to_r.stimulate->seq);

        auto const ended = r_answers ? walk.no_progress(r, 100) : walk.timed_out(to_r.stimulate->seq, 100);
        CHECK(ended.confirm_tip == r_answers);
        CHECK(ended.retry.has_value() == ! r_answers);
    }
}

TEST_CASE("header walk: an answer after the debt confirms as before",
          "[header_walk][debt][evidence]") {
    // The negation, through the same inputs: the debt comes while Q is being
    // asked and R, asked after it, answers. That answer is the evidence.
    auto walk = three_at(100);
    auto const to_q = walk.no_progress(p, 100);
    REQUIRE(to_q.stimulate);
    walk.delivered(to_q.stimulate->seq);
    (void)walk.owe(100);
    auto const to_r = walk.no_progress(q, 100);
    REQUIRE(to_r.stimulate);
    walk.delivered(to_r.stimulate->seq);

    auto const answered = walk.no_progress(r, 100);
    CHECK(answered.confirm_tip);
    CHECK_FALSE(answered.retry.has_value());
}

TEST_CASE("header walk: a debt does not loop on peers whose sends keep failing",
          "[header_walk]") {
    header_walk walk;
    (void)walk.restart(100);
    auto step = walk.peers_changed(peers_at(100, {p}), 100);
    REQUIRE(step.stimulate);
    (void)walk.owe(100);

    // P's send fails: the walk restarts once for the debt and asks P again.
    step = walk.send_failed(step.stimulate->seq, 100);
    REQUIRE(step.stimulate);
    // It fails again: now it parks, owing, until the peer set changes.
    step = walk.send_failed(step.stimulate->seq, 100);
    CHECK_FALSE(step.stimulate);
    CHECK_FALSE(step.confirm_tip);
    CHECK(walk.owed());

    auto const resumed = walk.peers_changed(peers_at(100, {p, q}), 100);
    REQUIRE(resumed.stimulate);
    CHECK(resumed.stimulate->peer == q);
}

// -----------------------------------------------------------------------------
// A valid prefix, then a rejected header
// -----------------------------------------------------------------------------

TEST_CASE("header walk: a prefix from a peer not being asked restarts a parked walk",
          "[header_walk][prefix]") {
    // Both peers were asked and neither answered in time: the walk is parked.
    // Then Q, not being asked, sends headers whose prefix moves the tip and
    // whose rest is rejected. The tip moved, so the walk restarts from it at
    // once — and asks P, not Q.
    auto walk = walking(100, 120);
    REQUIRE(walk.timed_out(walk.active()->seq, 100).stimulate);
    auto const parked = walk.timed_out(walk.active()->seq, 100);
    REQUIRE(walk.parked());
    REQUIRE_FALSE(parked.stimulate);
    REQUIRE_FALSE(parked.confirm_tip);
    REQUIRE_FALSE(walk.active());

    auto const step = walk.progress_then_failure(q, 105);
    REQUIRE(step.stimulate);
    CHECK(step.stimulate->peer == p);
    CHECK_FALSE(step.confirm_tip);
    CHECK(walk.spent(q));
    CHECK_FALSE(walk.spent(p));
}

TEST_CASE("header walk: failure alone on the same input leaves the parked walk parked",
          "[header_walk][prefix]") {
    // The negation, through the input the coordinator used to give it: the
    // prefix is lost and nothing is asked.
    auto walk = walking(100, 120);
    REQUIRE(walk.timed_out(walk.active()->seq, 100).stimulate);
    REQUIRE_FALSE(walk.timed_out(walk.active()->seq, 100).stimulate);
    REQUIRE(walk.parked());

    auto const step = walk.failure(q, 105);
    CHECK_FALSE(step.stimulate);
    CHECK_FALSE(walk.active());
}

TEST_CASE("header walk: a prefix from a peer not being asked leaves the active stimulus alone",
          "[header_walk][prefix]") {
    auto walk = walking(100, 120);
    auto const p_stimulus = *walk.active();

    auto const step = walk.progress_then_failure(q, 105);
    CHECK_FALSE(step.stimulate);                // one stimulus at a time
    REQUIRE(walk.active());
    CHECK(*walk.active() == p_stimulus);
    CHECK(walk.spent(q));

    // When P's ends, the walk does not go back to Q: P's answer from 105 is
    // the evidence, and with Q spent nobody is left to ask.
    auto const next = walk.no_progress(p, 105);
    CHECK_FALSE(next.stimulate);
    CHECK(next.confirm_tip);
}

TEST_CASE("header walk: a prefix from the active peer moves on to the next, once",
          "[header_walk][prefix]") {
    auto walk = walking(100, 120);
    auto const p_stimulus = *walk.active();

    auto const step = walk.progress_then_failure(p, 105);
    REQUIRE(step.stimulate);
    CHECK(step.stimulate->peer == q);
    CHECK(step.stimulate->seq == p_stimulus.seq + 1);   // exactly one new stimulus
    CHECK(walk.spent(p));
}

// -----------------------------------------------------------------------------
// Every peer timing out: a parked walk that nothing else would move
// -----------------------------------------------------------------------------

TEST_CASE("header walk: every peer timing out parks without evidence and schedules a retry",
          "[header_walk][retry]") {
    // P and Q are asked in turn and neither answers in time. Nobody said the
    // tip is current, so it is not confirmed; nobody is left to ask, so the
    // walk parks — and, the peers still being there, it asks for a retry.
    auto walk = walking(100, 100);
    auto const to_q = walk.timed_out(walk.active()->seq, 100);
    REQUIRE(to_q.stimulate);
    auto const parked = walk.timed_out(to_q.stimulate->seq, 100);
    CHECK_FALSE(parked.confirm_tip);
    CHECK_FALSE(parked.stimulate);
    REQUIRE(parked.retry.has_value());
    CHECK(parked.retry->attempt == 0u);
    CHECK(walk.parked());

    // The retry: a walk from scratch, P first again.
    auto const again = walk.retry_due(parked.retry->generation, 100);
    REQUIRE(again.stimulate);
    CHECK(again.stimulate->peer == p);
    CHECK_FALSE(walk.parked());
    CHECK_FALSE(walk.spent(q));

    // And this time they answer: the tip is confirmed, on evidence.
    REQUIRE(walk.no_progress(p, 100).stimulate);
    CHECK(walk.no_progress(q, 100).confirm_tip);
}

TEST_CASE("header walk: some answering and the rest timing out is evidence, not a park",
          "[header_walk][retry]") {
    // P answers "nothing past your tip"; Q answers nothing. At the height they
    // claim, P's answer is the evidence: provisional confirmation, no retry.
    auto walk = walking(100, 100);
    auto const to_q = walk.no_progress(p, 100);
    REQUIRE(to_q.stimulate);
    auto const done = walk.timed_out(to_q.stimulate->seq, 100);
    CHECK(done.confirm_tip);
    CHECK_FALSE(done.retry.has_value());

    // Below the height they claim, the same answers are just as much evidence:
    // the claim is not, and Q's silence backs it with nothing. Confirmed, and
    // nothing polled on the claim's behalf.
    auto behind = walking(100, 120);
    auto const to_q2 = behind.no_progress(p, 100);
    REQUIRE(to_q2.stimulate);
    auto const confirmed = behind.timed_out(to_q2.stimulate->seq, 100);
    CHECK(confirmed.confirm_tip);
    CHECK_FALSE(confirmed.retry.has_value());
}

TEST_CASE("header walk: a retry meant for an earlier park does nothing",
          "[header_walk][retry]") {
    auto walk = walking(100, 120);
    REQUIRE(walk.timed_out(walk.active()->seq, 100).stimulate);
    auto const parked = walk.timed_out(walk.active()->seq, 100);
    REQUIRE(parked.retry.has_value());

    // A new peer moves the walk on before the retry is due.
    auto const moved = walk.peers_changed(peers_at(120, {p, q, r}), 100);
    REQUIRE(moved.stimulate);
    CHECK(moved.stimulate->peer == r);
    auto const active = *walk.active();

    auto const stale = walk.retry_due(parked.retry->generation, 100);
    CHECK_FALSE(stale.stimulate);
    REQUIRE(walk.active());
    CHECK(*walk.active() == active);

    // R times out too: a NEW park, with its own generation. The old retry
    // still does nothing; the new one does.
    auto const reparked = walk.timed_out(active.seq, 100);
    REQUIRE(reparked.retry.has_value());
    CHECK(reparked.retry->generation != parked.retry->generation);
    CHECK_FALSE(walk.retry_due(parked.retry->generation, 100).stimulate);
    CHECK(walk.retry_due(reparked.retry->generation, 100).stimulate.has_value());
}

TEST_CASE("header walk: the backoff grows while nobody answers and starts over on an answer",
          "[header_walk][retry]") {
    auto walk = walking(100, 120);

    // Park, retry, park, retry: the attempt count is what the caller backs off on.
    std::vector<uint32_t> attempts;
    for (int round = 0; round < 3; ++round) {
        REQUIRE(walk.timed_out(walk.active()->seq, 100).stimulate);
        auto const parked = walk.timed_out(walk.active()->seq, 100);
        REQUIRE(parked.retry.has_value());
        attempts.push_back(parked.retry->attempt);
        REQUIRE(walk.retry_due(parked.retry->generation, 100).stimulate);
    }
    CHECK(attempts == std::vector<uint32_t>{0, 1, 2});

    // P answers this time — with headers that are rejected, which is an answer
    // but not evidence — and Q does not. Parked again, but the answer started
    // the backoff over.
    REQUIRE(walk.failure(p, 100).stimulate);
    auto const parked = walk.timed_out(walk.active()->seq, 100);
    REQUIRE(parked.retry.has_value());
    CHECK(parked.retry->attempt == 0u);
}

TEST_CASE("header walk: with no peers connected there is nothing to retry",
          "[header_walk][retry]") {
    header_walk walk;
    auto const step = walk.restart(100);
    CHECK_FALSE(step.retry.has_value());
    CHECK(walk.parked());

    // And the peer arriving is the event that moves it.
    CHECK(walk.peers_changed(peers_at(100, {p}), 100).stimulate.has_value());
}
