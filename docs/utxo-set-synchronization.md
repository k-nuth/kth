# UTXO set synchronization

How block connection, UTXO set construction and mempool admission are
coordinated, and what the data structure holding the set imposes on all three.

## How to read this

Every claim is marked as one of two things:

- **[TODAY]** — the current state of the code, verified.
- **[TARGET]** — where we want to get.

The distinction matters because the target design and the current code differ in
ways that matter, and a document that mixes them reads, six months later, as a
description of what exists.

The end goal is **no mutexes**. The exclusion stage 1 proposes is a transitory
correctness fix, not the final architecture.

---

## 1. The central rule

The UTXO set store (UTXO-Z) admits **N concurrent readers or one writer, never
both**. It has no internal locking and will not grow any: the barrier is the
node's responsibility.

### Why a reader during a mutation is fatal rather than imprecise

When the active map fills, an insert triggers a **rotation**: the whole mapped
segment is closed and unmapped, and the pointer to the map is left dangling or
null for a window. A concurrent reader is not reading a torn value — it is
accessing unmapped memory, which is undefined behaviour and can end in SIGSEGV,
in garbage, or in anything else.

A delete additionally perturbs the open-addressing probe chains, so a concurrent
reader can get a **false negative on a key that has nothing to do** with the one
deleted. That is the dangerous direction: an existing prevout reported absent.

One hypothesis is worth recording as **false**: the danger is not a rehash of the
container. Rotation pre-empts the rehash by design (it triggers at a lower load
factor), and it is measured: zero rehashes over 595,000 inserts. The active map's
bucket array never moves. What moves is the whole mapping.

### Why a concurrent container does not fix it

Replacing the active map with a concurrent container was evaluated and rejected:

- It fixes the element-level race, **not** the rotation's unmap, which is the
  one that reaches unmapped memory. No map type protects you from having its
  segment unmapped underneath.
- Its internal locks would live inside the persisted file. A process that dies
  holding one leaves a locked mutex written to disk, and the next open
  deadlocks. For a UTXO store that is unacceptable.
- It has no iterators. The code uses them in compaction, in the sweeps, and in
  general iteration.

---

## 2. Current state

### Who touches the UTXO set

Everything goes through a single layer; nothing in the C API or RPC touches it
directly.

**Readers — three:**

| caller | when |
|---|---|
| admission's prevout resolution | outside IBD, when a transaction arrives |
| block validation's prevout resolution | per batch |
| undo capture | per batch, above the checkpoint |

**Writers and rotators — all in the block-connect path**, plus the inverse delta
applied during a reorg: delta application, deferred deletions, historical
resolution, compaction.

### What runs in parallel today

**[TODAY]** Within the block-connect path, the UTXO set is touched by **a single
thread end to end**. The parallel part — script and signature verification —
**does not touch the set**: it works on prevouts already resolved in memory. That
parallelism is safe by construction, not by discipline.

**[TODAY]** Block download, parsing and fast validation run in parallel and do
not touch the set.

**[TODAY]** A batch's prevout collection and its undo capture walk every input in
a **serial** loop. There is no design reason: they are reads.

**[TARGET]** Both walks in parallel, accumulating per thread. What **cannot** be
parallelized is historical resolution, and that constraint is real: it comes from
the store's mapping cache, not from a decision of the node's.

### Order within a batch

**[TODAY]**

1. Read the raw blocks off disk.
2. Above the checkpoint: walk every input and probe the active map (serial).
3. One historical resolution for whatever the active map did not hold.
4. Verify scripts and signatures (parallel, without touching the set).
5. Build the delta.
6. Capture undo data — **with a second historical resolution**.
7. Apply the delta.
8. Process deferred deletions.
9. Persist undo and markers.
10. Tell the mempool.

Batches are up to 1000 blocks below the checkpoint; above it they are capped at
100, because the whole batch's undo is held in memory. Outside IBD the batch
drains down to a single block, so each new block is processed as it lands.

### Known defects

**[TODAY] Mempool admission cannot resolve a prevout that needs history** (issue
#584). The active-map probe answers every miss the same way, whether or not the
prevout exists in an older version — it cannot establish absence, only that the
active map does not hold the key. Admission reads that answer as absence and
never runs the second phase, so the transaction is rejected with *previous output
not found*.

How much that costs in production is **not established** and belongs in the
measurements below: it depends on what fraction of spends resolve straight out of
the active map. What is measured is that the deferred path is not exotic — the
node's own test harness has to drain the queue to find a UTXO that exists, and
ten of ten admissions of matured-coinbase spends failed there.

**[TODAY] Two callers can return leaving the pending queue dirty.** Both undo
capture and batch validation have error returns **inside the loop** that walks the
inputs, and by the time they are reached, earlier iterations have already queued
their keys. They return without sweeping.

**[TODAY] History is resolved twice per batch.** Step 3 resolves prevouts in
order to validate; step 6 resolves **the same spent outputs** to capture undo.
Two full passes over the version files. The two want the data in different shapes
— one the resolved output, the other the payload as stored — so unifying them is
not free, but it is an entire pass being paid for twice.

**[TODAY] A transaction's signature verification runs under an exclusive lock.**
Admission takes the lock before the context-free checks and does not release it
until after publishing to the mempool. Inside that window sit prevout
resolution, the contextual checks and — the expensive part — script
verification. **Two transactions never validate in parallel.**

**[TODAY] The exclusion is held across coroutine suspension points, which is
undefined behaviour.** `transaction_organizer::organize` takes the mutex, then
awaits the context-free checks, the contextual ones and script verification, then
releases it. Two of those awaits post work to the priority pool — up to eight
threads — and wait on a channel, so they genuinely suspend. A `shared_mutex` may
only be released by the thread that took it, and a coroutine may resume on
another. Whether it currently misbehaves depends on how the awaiting coroutine's
executor happens to be configured, which is not a property to rely on.

It is live, not latent, and not only on the path that publishes: every exit after
those awaits releases the mutex, so a rejected admission runs it too. Admission
is reachable today over JSON-RPC and through the C API. The block-connect path
has the same shape — its batch validation is awaited across the pool — so both
sides need the same treatment.

**[TODAY] The exclusion primitive has no shared mode.** The node's priority mutex
takes the same exclusive lock on both of its paths; priority only decides who
enters next when there is a queue. It gives one-at-a-time, not
N-readers-or-one-writer. Its three internal mutexes are already of a type that
would admit shared readers, but all three are used exclusively, and two of them
implement the turn rather than protecting data.

---

## 3. Stage 1 — correctness with the exclusion that already exists

**[TARGET, immediate]** Transitory and deliberately so: it reuses the existing
exclusive mutex, introduces none of stage 2's atomic gate, and depends on no
change to the store.

It does add coordination — the four windows, the generation, readiness and the
dependency re-check — and that is the point of writing the contract down before
implementing it.

### The constraint that shapes it

**No mutex may be held across a suspension point.** Both paths are coroutines and
both suspend in the middle of the work that has to be exclusive, so "take the
lock and hold it across the sequence" is not available: the thread that releases
would not always be the thread that took it.

So the exclusion is expressed as **windows**, each one entirely synchronous, with
the suspending work outside them. The windows are chosen so that nothing partial
is ever observable between them.

It helps that the phases do not all need the same thing. The store admits N
readers *or* one writer, and validation only reads. What must not overlap is a
reader with a writer, and one historical resolution with another.

### The block-connect path: two windows

**W1 — prevout resolution.** Emit the active-map probes for every input of the
batch and run the one historical resolution. Both are synchronous and on the same
thread, so the window closes before anything suspends.

Released before script verification, which touches nothing shared: it works on
prevouts already copied into memory.

**W2 — connection.** Undo capture, the delta, deferred deletions, markers, the
generation bump and the mempool cleanup. All synchronous; verified that the batch
sequence suspends only at validation, which is over by now.

Between W1 and W2 nothing has been applied, so an admission that runs there sees
the pre-block state — coherent, if about to be superseded. The half-applied
states worth worrying about — an output the block spends still present because
its deletion has not run — are all inside W2.

### The admission path: two windows and a generation

**A1 — resolution.** Take the exclusion, probe the active map for every prevout,
run the one historical resolution, resolve unconfirmed parents from the mempool
overlay, and **copy every prevout out**. Record which of them came from the
mempool and the parent transaction each came from. Capture the validation-state
generation. Release before anything suspends.

**Verification, unlocked.** Scripts and signatures, against the copies. A block
may connect while this runs.

**A2 — publication.** Retake the exclusion and check, before publishing:

- the **validation-state generation** is the one A1 captured;
- every dependency taken from the mempool is still present and still the same
  output.

Only then publish. Two distinct reasons to fail, both ending in the result being
discarded rather than published — whether to retry against the new state is
policy, subject to the same limits any retry is:

- the chain or UTXO state moved;
- an unconfirmed dependency vanished or changed.

### The validation-state generation

A counter representing the state transactions are validated against. It is not
the mempool's generation and not the active chain's: it must advance on **every**
change to that state, and only under the exclusion that publishes the change —

- a normal block connection;
- a disconnect or a reorg;
- adopting the state a reorg leaves behind;
- any rebuild or equivalent replacement of the active UTXO set.

It is deliberately named for validation state rather than for the UTXO set,
because it also invalidates the height and the consensus context a transaction
was checked under.

**Why the mempool's own generation is not the test for unconfirmed parents.** It
moves on every admission, so an unrelated one would invalidate work that is still
perfectly good. Checking the specific dependencies costs a lookup each and
invalidates only what actually broke. A global counter would be a correct first
implementation, just a needlessly conservative one.

### Readiness: the generation proves movement, not coherence

A generation that has not changed says nothing moved between A1 and A2. It does
not say the state A1 read was coherent to begin with, and there is a window where
it is not.

After a reorg the UTXO set can sit rewound near the fork while the active chain
already names the winning branch and the build has not caught up. Staleness is
judged from the top block's timestamp, so a recent tip after a reorg reads as not
stale and admission stays open. A1 could then resolve prevouts against the older
UTXO state while taking height and consensus flags from the newer tip — a
combination that never existed as a connectable tip. Nothing moves between A1 and
A2, the generation matches, and the transaction is published.

So A1 needs a **readiness** condition on entry, re-checked in A2 alongside the
generation. It has two halves, and both are needed:

**A coherent local view** —

- no reorg in progress;
- the UTXO set is built to exactly the height being used as context;
- that height, its hash and its consensus context come from the same view.

**And a synchronized node.** Coherence is not enough on its own: while catching
up, the set can momentarily reach the newest tip the node happens to know, and
all three conditions above hold while that tip is still old and headers are
still coming. The state is internally consistent and months behind. Admitting
against it produces decisions no peer would agree with, which is why blocking
during IBD was about relay stability and not only about internal coherence.

    ready = coherent_local_view && synchronized

with the reorg condition already inside the first, not a third term beside it.

Absent a formal signal that IBD has finished, `synchronized` starts as the
existing staleness heuristic. It proves nothing about coherence — that is what
the first half is for — but it remains the answer to a different question, and
readiness needs both answers.

**One consequence for the generation:** it advances once per coherent state
published, not once per internal step. A reorg's disconnect, adoption and rebuild
are steps of a single transition, and any of them can leave a state that is not
valid to admit against. During such a transition admission stays closed on
readiness — treating each step as a publishable state would be exactly the bug
above, with extra steps.

### Why this covers the interleavings

- **A2 before W2:** the transaction is published against the older state, and W2's
  mempool cleanup removes it if the block confirmed it or conflicts with it.
- **W2 before A2:** the generation moved, so A2 does not publish a stale result.
- **Admission starting between W1 and W2:** it validates coherently against the
  pre-block state, and the generation decides afterwards whether it may still
  publish.

### What it buys

- No reader is inside the store while it is mutated or rotated.
- The window between validating a transaction and publishing it closes.
- **The global pending-lookup queue gets a single owner at a time**, which is what
  makes it safe for admission to sweep. That closes #584 without waiting for any
  change to the store.

### Why the queue's ownership holds

Only one thing can be inside a window at a time, so nobody can consume anyone
else's results. It rests on two conditions, **both necessary**:

1. **Every** user of the store participates. A single one outside invalidates the
   property.
2. **An unconditional epilogue** leaves the queue empty before releasing, even on
   errors and cancellations — by RAII or a single exit path, never by returns
   that remember to sweep.

A window tolerates the queue getting dirty inside it, because the same owner
drains before closing. It does not tolerate it getting dirty on the way out.

### Cost

Admission is stalled during W1 and W2, not during script verification — its own
or the block's. Outside IBD that is one block every ten minutes. During IBD it
would still matter, because W1 covers a batch of up to a hundred blocks — but
readiness is false throughout IBD, through its synchronization half, so admission
is closed rather than queued behind batches. That gate is part of stage 1's scope
rather than a later target: without it, RPC can still admit during IBD today.

One cost is accepted deliberately: the generation is coarse, so **any** block
connecting between A1 and A2 discards the transaction, even one that touched none
of its prevouts. That is signature work thrown away. Outside IBD it is a block
every ten minutes and during IBD admission is closed, so it is not worth
refining now — but it is a decision, not an oversight. Narrowing it to "did the
block touch *our* prevouts" is a later optimization.

### `unresolved` is not absence

Until the store distinguishes "does not exist" from "a version could not be
read", a sweep that fails to find a key **is not proof that it does not exist**.
For admission that means not admitting the transaction **without asserting** the
prevout is missing: a resolution failure, not a rejection for invalidity.

This is narrower than it may look, and invariant 4 below is marked blocked for
exactly that reason. Undo capture already separates an immediate read failure
from "queued for the sweep" — but after the sweep has run, a key that did not
come back is still indistinguishable from one whose version could not be read.
Closing that needs the store's third result.

### Admissions do overlap, and that has to be chosen

A1 and A2 are exclusive, but the verification between them is not. A second
admission can run its own A1 and both can verify signatures at the same time.
This is worth having — it is most of what stage 2 was for — but it is a choice,
not a side effect, and it has a prerequisite.

**The per-transaction validation scratch is not safe for it today.** It lives in a
plain `boost::unordered_flat_map`, and the header says why that was fine:

> mutex held across their co_awaits, so they are mutually exclusive

That is precisely the property being removed. Once two admissions overlap, they
touch that map concurrently — the contextual checks write an entry and script
verification reads it back. Either it becomes concurrent, or the scratch stops
living in a shared store at all, which is the better shape: nothing reads an
entry once its admission returns, so it is private to the admission and only
happens to be kept somewhere shared.

The audit is the rest of the admission path under overlap: the context-free
checks, the contextual ones, script verification, and anything else reading that
scratch — block validation uses the same class of store.

The alternative is to forbid the overlap, and that needs serialization that is
not bound to a thread — a strand, an async semaphore, a queue. The existing mutex
cannot express it, which is the whole reason for the windows.

### What stage 1 does NOT do

- It does not remove the mutex.
- It does not parallelize the block path's probes.

It is the correctness floor to measure from.

---

## 4. Measurement

With admission able to resolve historical prevouts, measure before building
anything else:

- transactions admitted per second, and in bursts;
- time under lock, split between prevout resolution, signature verification and
  publication;
- block-connect latency, **p99 rather than mean** — the active map's rotation is
  the entire tail, and the containers holding large values rotate often;
- how often a lookup needs history;
- the Bloom filter's effect once it exists.

If serial admission meets the real load, stage 2 is deferred. If it does not,
there will be data to choose between the options rather than intuition.

---

## 5. What is asked of the store

**[TARGET]** Make the difference between querying the active map and resolving
historical versions explicit.

**Active map probe.** `const`, queues nothing, does not touch the version cache,
concurrent with other probes, and returns explicitly **found**, **needs
historical resolution** or **error**. Never "does not exist": querying only the
active map cannot establish that.

**Historical resolution.** Takes an explicit list owned by the caller — the store
keeps no queues. Returns **three** sets: found, authoritatively absent, and
**unresolved** because a version could not be opened or read. A read error may
never degrade into absence. Found values are copied before releasing any mapping;
no result may hold a reference into mapped memory.

**Deletes.** The same model, with explicit results rather than an ambiguous
boolean.

**Exclusivity.** Historical resolution of lookups, of deletes, rotation,
compaction, and anything that can open or evict mappings are mutually exclusive.
Ideally the store detects two concurrent resolutions in debug builds.

**Statistics.** They must not prevent concurrency between probes, nor grow
without bound. Sharded atomic counters, aligned to 128 bytes so shards share no
cache line on either x86 or Apple Silicon.

**Version cache and Bloom filters.** The cache size must be measured, not picked
by eye. A per-version filter would avoid mapping files that certainly do not hold
a key — false positives only add a query, false negatives are unacceptable. It
helps both single-key lookups and reducing how many files a batch resolution
opens.

**Open question:** if a single historical pass could return both views needed —
the materialized output for validation, and the stored payload with its original
height for undo — an entire sweep per batch disappears.

---

## 6. Stage 2 — concurrent admission without mutexes

**[TARGET, later]** Only after the store's API change, and only if measurement
justifies it.

### The barrier

A **single atomic word** encoding both the writer-pending bit and the count of
active readers:

```
[ writer_pending | reader_count ]
```

A reader increments by CAS only if the bit is clear; a block sets the bit by CAS
and waits for the count to reach zero, using atomic wait/notify rather than
polling.

A single word was chosen over two separate atomics. With two, the protocol is the
classic store-buffer pattern — each side writes one variable and reads the other
— and **is only correct under sequential consistency on that pair**. Under weaker
ordering the two sides can miss each other, and the block mutates with a reader
inside. This is not simply "x86 is fine, ARM is not": whether it holds depends on
the specific operations and on the fences a read-modify-write happens to imply,
so it may appear to work under a stronger memory model and fail under a weaker
one — which is the worst way to find out. A single word has one modification
order, so the coordination race between two states disappears, rather than
resting on nobody "optimizing" the ordering tags later.

That does **not** mean ordering stops mattering. The word settles who may enter;
publishing and observing the data it protects still needs proper
acquire/release — a reader must see the mutations the writer made before it
released, and the writer must see everything the readers did before they left.

**What the single word does not solve:** cancellation and generation remain
separate reads, with their own reasoning. The CAS covers entry and exit, not the
whole protocol.

**The tension to settle with data:** the single word is provable and has one hot
cache line; sharded counters reduce contention but reintroduce the cross-state
coordination the single word removes, and on the writer's side. **The provable
design and the low-contention design pull in opposite directions.**

It is also worth not fooling ourselves about the premise: a conventional shared
lock carries an atomic counter inside too, so the hot spot is the same. What a
hand-rolled mechanism buys is not fewer atomics, it is **behaviour**: absolute
priority for blocks, queueing instead of blocking threads, cooperative
cancellation, and integration with generations.

### Cancellation

A function already executing cannot be stopped safely. Cancellation is
cooperative: checks at safe points — after resolving prevouts, before starting
scripts, between signature groups, before publishing — that abandon and requeue
the transaction.

A signature verification already under way is best left to finish, with its
result discarded. The property needed is not that a block interrupts any
instruction, but that **with a block waiting, no new work starts, existing work
reaches a cancellation point quickly, and the block does not mutate until every
reader has left**.

### What the slot must cover

A single slot from entry to publication is simpler and less error-prone, and is
where to start. But these are **two distinct requirements with two distinct
windows**, worth knowing before anyone tries to shorten the gate:

- **Store safety:** only the window where mappings are read needs protecting.
  Once prevouts are copied, verifying signatures does not touch the store.
- **Coherent publication:** lasts until the mempool write completes, and checking
  the generation is not enough — the window between checking and publishing
  remains.

### Admission queue

It accumulates transactions while a block connects, forms batches, applies
backpressure, and retries what a generation change cancelled. It fires on count,
on timeout, on memory pressure, when a block finishes, or when the node stops
being stale.

**It is new attack surface.** Sending transactions at a steady rate is enough to
keep it full while a block connects. It needs a maximum count, a maximum memory
footprint, a timeout, deduplication, an explicit discard policy, metrics,
invalidation by generation, and a per-origin limit once provenance exists.

**It is not built in stage 1.** With a Bloom filter, historical resolution may
become cheap enough to do inline, and then the queue is never needed.

---

## 7. The mempool

**[TODAY] It is already concurrent.** Both of its maps are concurrent by default,
and admission is already written for concurrency: it claims every spent outpoint
with an atomic insert and, if any fails, rolls back the ones already claimed and
rejects. First-seen is already atomic per outpoint, with no external lock.

The header comment saying writes are serialized by the organizer's mutex and so
need no internal locking **is stale with respect to the implementation**, and
should be corrected.

**[TODAY] What does need ordering are the removals.** Removing for a block,
removing recursively, and updating for a reorg are multi-step walks — they look
up who spends an outpoint, decide, then erase from both maps — which are not
atomic and do race a concurrent admission. They belong inside the block's
exclusive section.

**[TODAY] A false rejection in admission.** Two transactions spending the same set
of outpoints in different orders can each claim one, both fail, and both roll
back: neither is admitted, when exactly one should be. There is no corruption and
no double spend.

**[TARGET]** Claim outpoints in **canonical order**, and detect duplicate inputs
within the transaction itself. With the keys sorted, two conflicting transactions
try the same key first: one wins cleanly and the other fails having claimed
nothing.

### The conflict branch is where a double-spend proof is born

Worth knowing before that code is touched again. Detecting a conflict is exactly
the moment the node learns of a double spend, which is what a proof attests to —
and admission today answers a conflict with a bare `false`. It reports neither
the outpoint that conflicted nor the pooled transaction holding it, and a proof
needs both.

Nothing asks for that yet: no path creates a proof, none ingests one, and none
validates one (#587). So this is a constraint on that work rather than something
to build ahead of it — when proofs are implemented, the conflict result grows
along with the code that consumes it.

Canonical claim order helps here too, beyond picking a winner: it makes *which*
outpoint this node reports as the conflict deterministic, so the same pair of
transactions always yields the same proof candidate locally.

It does not give a proof its identity, and it should not be read that way. That
comes from the specification's ordering of the two spenders — by hash-outputs,
and by hash-prevoutputs where those tie — which is a separate thing this code
does not touch. Nor does it bind anyone else: when two transactions conflict on
several outpoints, another implementation may pick a different one and build a
different, equally valid proof.

### A block is not valid because its transactions are in the mempool

That a transaction is pooled means it was validated against some chain state. A
block carrying it must still verify the header and proof of work, the connection
to the right parent, the merkle root, the coinbase, the subsidy and fee sum, the
block size and sigcheck limits, the contextual rules active at that height,
coinbase maturity, the absence of duplicate prevouts, and the internal
dependencies the rules permit.

Parsing, hashes, already-resolved prevouts, fee, size, sigchecks and even script
results can be reused — **if the context is equivalent**. A validation cache has
to key on at least txid, chain generation, consensus flags and the prevouts used.

A transaction in a block that the node never saw does not imply a double spend:
it may simply be valid. And a conflict with a transaction that only exists in the
mempool does not invalidate the block — the block wins, and connecting it evicts
the conflicting transaction and its descendants.

---

## 8. Admission during IBD

**[TARGET]** Do not accept new admissions unless the validation state is ready
(section 3). A mempool restored at startup stays pending and is revalidated once
it is.

Readiness rather than staleness alone, and neither replaces the other. Staleness
misses the reorg window — a recent tip reads as not stale while the set is still
rewound — and coherence misses IBD, since the set can momentarily catch up to an
old tip while headers are still arriving. Readiness is the conjunction.

It should not be informally assumed that the mempool is empty during IBD: there
may be admission over RPC, or a restored pool.

The reason is not only concurrency. With the tip far from current, a transaction
is validated against a state that represents the local tip but goes stale
continuously, and is no use for stable relay.

---

## 9. Invariants, with state

| # | invariant | state |
|---|---|---|
| 1 | No reader of the active map while it is rotated or modified | pending — stage 1 |
| 2 | One executor touches the historical version cache | pending — stage 1 |
| 3 | No result exposes references into a mapping that can be evicted | to confirm in the store |
| 4 | A historical read error is never reported as absence | blocked on the store's API |
| 5 | Blocks take priority over admission | pending — stage 1 |
| 6 | A transaction is not admitted on results from a stale generation | pending — stage 1 (stage 2 must preserve it) |
| 7 | A batch's delta is applied in chain order | **implemented** |
| 8 | Undo and markers persist in an order that allows recovery | **implemented** |
| 9 | The mempool is updated only after a block actually connects | **implemented, tested** |
| 10 | Mempool membership does not replace a block's contextual validation | **holds** — no reuse path exists yet; a requirement on any future one |
| 11 | No exit leaves the pending-lookup queue dirty | pending — stage 1 |
| 12 | No mutex is held across a coroutine suspension point | **violated today** — stage 1 |
| 13 | The validation-state generation advances once per coherent state published, under the exclusion that publishes it | pending — stage 1 |
| 14 | A transaction is only validated against a state that existed as a connectable tip | pending — stage 1 |

---

## 10. Order of work

1. This document.
2. Canonical outpoint ordering and duplicate-input detection in mempool
   admission. Independent of everything else.
3. **One PR**: the four windows, #584, and queue sanitation. W1/W2 on the
   block-connect path and A1/A2 on the admission path, each synchronous and none
   crossing a suspension; the validation-state generation and the mempool
   dependency check; the queue empty on entry and before release, drained
   unconditionally on every early exit, with the primary error kept distinct from
   the drain's result; `unresolved` treated as a resolution failure; and admission
   gated on readiness — a coherent local view and a synchronized node.
4. Measure.
5. Explicit store API and Bloom filters.
6. Stage 2, only if the data justifies it.

Cleaning up the dirty exits **does not go as a separate PR**: without the
barrier, the drain an epilogue performed would also take keys queued by another
caller, and the drain itself is a historical resolution, which must be exclusive.
It would fix the leak without establishing ownership.

### Tests PR 3 must bring

The queue's discipline:

- Undo capture queues a key and fails on a later iteration: on the way out, the
  queue is empty.
- Batch validation, the same, failing on a double spend.
- A later operation does not receive results belonging to the previous one.
- The same cases on normal returns, not only on errors.

That admission can resolve at all:

- A prevout living outside the active map ends up admitted — the direct
  regression for #584.
- A historical read failure does not turn into *previous output not found*.

**The state machine, and none of it probabilistic.** If A1, A2 and the readiness
evaluation are separable operations with contracts of their own, every decision
below is a direct test — no artificial pause in production code, no waiting on a
scheduler:

- A1 captures a generation; it is bumped before A2; A2 does not publish.
- A1 captures a mempool dependency; that parent is removed before A2; A2 does not
  publish.
- A1 and A2 see the same generation and the same dependencies; A2 publishes.
- A1 refused: a reorg is in flight.
- A1 refused: the set's built height and the context height disagree.
- A1 refused: the local view is coherent but the node is still stale — the case
  coherence alone would let through.
- Two admissions pass through the unlocked phase at once and share no scratch.
- A2 before W2: the transaction is published, and W2 removes it when it should.
- W2 before A2: the generation moved, so A2 does not publish.

And one against the defect that started this: run an admission with its
continuations landing on different threads. It does not prove the absence of
undefined behaviour on its own, but it is what stops the mutex being held across
the awaits again.

What stays probabilistic is only the scheduling — whether a particular real
interleaving occurs under load, which the concurrency test covers under a thread
sanitizer. The state machine itself should not be, and writing it as separable
operations is what buys that.
