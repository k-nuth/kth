# What the node needs from the UTXO store

A requirements document, not an implementation plan. It describes the interface
the node needs in order to validate transactions in parallel, and the guarantees
that interface has to carry. It is written to be handed to whoever implements the
store, and reviewed before either side writes code.

## The goal this serves

Bitcoin Cash scales when transaction validation scales, and transaction
validation scales when it is parallel. Everything below exists to remove
serialization from the path a transaction takes, and to state plainly the few
places where serialization is unavoidable.

The path, as [the synchronization contract](utxo-set-synchronization.md)
describes it:

```
A1  resolve the transaction's prevouts     ← must be parallel across transactions
    verify scripts and signatures          ← already parallel, touches no store
A2  check nothing moved, publish           ← must be parallel across transactions
```

Signature verification dominates the cost, and it does not touch the store. What
the store decides is whether A1 can run for many transactions at once, or one at
a time.

## 1. The concurrency contract

The store's data model admits **N concurrent readers, or one writer, never
both**. The node owns the barrier that enforces this; the store does not lock
internally and is not asked to.

What the node needs stated as a guarantee, so it can be relied on rather than
inferred:

| operation | required guarantee |
|---|---|
| probe | any number of threads at once, provided no mutation is in flight |
| historical resolution | see below — the shape must not preclude concurrency |
| insert, erase, delta application | one writer, no readers |
| rotation | one writer, no readers (it happens inside insert) |
| compaction | exclusive with everything |
| size, statistics | must not require exclusion against probes |

**Historical resolution is the one to get right.** Today it can only have a single
executor, because it opens version files through a cache that has no
synchronization and whose eviction unmaps memory another thread may be reading.
That is an implementation property, not something the interface should bake in.
The shape below — caller-owned request lists, results that own their data, no
shared queue — is already free of cross-caller coupling, so the guarantee can be
strengthened from "one at a time" to "concurrent" later **without changing a
single signature**. Please keep it that way.

Whether it starts serialized is a judgement call for the implementer. Section 3
is what makes that acceptable.

## 2. Probing the active map

The single most important operation, because it is the one every admission runs
and the one that must scale.

```
probe(key, height) -> found(value) | absent | needs_resolution | error
```

Requirements:

- `const`, and safe from any number of threads at once.
- Queues nothing. Keeps no per-caller state. Has no side effects a later call
  can observe.
- Touches no file cache and opens no file.
- Never blocks on another caller.

### The four answers, and why `absent` must be one of them

Today's interface can only say "in the active map" or "not in the active map",
and the second is reported with the same code that means "does not exist". That
is the direct cause of the node rejecting valid transactions: a miss reads as
absence.

The fix is not merely to rename the miss. It is to make **authoritative absence
answerable from the probe**, because a probe that can only ever say "I don't
know" forces every miss into the serialized path, and that is exactly what stops
A1 from scaling.

The four answers:

- **found** — in the active map. Authoritative.
- **absent** — not in the active map, and provably not in any older version.
  Authoritative, and reached without opening anything.
- **needs_resolution** — not in the active map, and it cannot be ruled out. The
  caller collects these for section 3.
- **error** — the store could not answer. Never to be conflated with `absent`.

### First, a question that must be answered before the signature is fixed

**What does `height` mean?** The current interface takes one on every lookup and
its own documentation answers "statistics only; does not affect the result". The
bound that actually matters — discarding an entry created above the height being
validated against — is applied by the node afterwards, by turning a too-new entry
back into a miss.

That is not a detail. Whichever answer is chosen decides:

- what `absent` means, and whether it is absence *as of a height* or absolute;
- which versions are relevant to a lookup, and so what may be ruled out;
- when two requests are the same request, and therefore what identity the output
  needs to carry;
- how any of this behaves across a reorg, when the height being validated against
  moves backwards.

The candidates:

1. **Selector** — the lookup answers as of that height, and the store discards
   entries created above it. Absence becomes absence *at a height*.
2. **Relevance bound** — the height narrows which versions must be consulted, but
   the answer is about the present.
3. **Context only** — statistics and age, as today, with the node keeping the
   bound.
4. **A check** — the store validates the stored height against it and reports a
   disagreement.

We have a preference for (1) or (2), because either lets the store rule out
versions and neither leaves a correctness-relevant filter outside the thing that
computed the answer. But whoever knows the storage layout is better placed to say
which is implementable without cost, so this is asked rather than asserted —
and it should be settled before anything below is turned into a signature.

### The requirement, stated without fixing the mechanism

What is needed is this, and nothing narrower:

> The probe must be able to **prove absence from memory, at bounded cost, without
> opening a version file.**

Per-version summaries are the obvious way, but walking one per historical version
is O(versions) per miss, which is the wrong shape once versions accumulate. A
hierarchical index, an aggregate summary backed by per-version ones, or something
else entirely are all acceptable if they meet the requirement. Per-version
summaries remain useful regardless, for deciding which files a batch resolution
opens — that is a separate use with no correctness weight.

### Integrity, because `absent` decides validity

This is the part that matters most, and it is a stronger requirement than it
looks. The moment a probe can answer `absent`, whatever it consulted becomes part
of the node's correctness boundary: a wrong `absent` rejects a valid transaction,
or worse, in a path that trusts it.

A Bloom filter has no false negatives *by construction*. It does have them in
practice when bits are corrupt, a file is truncated, or the summary belongs to a
different version than the one it is being used for. So `absent` may be returned
only when **all** of these hold:

- the catalogue of relevant versions is complete;
- every one of them has a valid summary bound to exactly its contents;
- version and summary were published atomically;
- the summary passed an integrity check;
- no compaction or rotation is changing the catalogue.

Anything else — a missing summary, a corrupt one, one from the wrong generation —
yields `needs_resolution` or `error`. **Never `absent`.**

Publication should be ordered so a partial state is never reachable:

```
build the version
build its summary from the exact final contents
persist both
verify metadata and checksum
publish the catalogue entry atomically
```

A sealed version must not have its summary modified in place. If compaction
changes what a file holds, it publishes a **new** version with a **new** summary.
An older summary may keep false positives — those only cost work — but must never
be able to acquire a false negative.

Summaries are read from many threads at once, so once published they must be
immutable, or otherwise safe for concurrent reads.

### Why this is the difference between scaling and not

With an authoritative in-memory answer for absence, the common case — a
transaction spending recent outputs — resolves entirely from memory,
concurrently, with no serialization at all. Only the rare key that cannot be
ruled out reaches the serialized path.

Without one, every spend of an older output serializes, and A1 becomes a queue.

## 3. Historical resolution

For the keys a probe could not settle.

```
resolve_lookups(span<request const>) -> { found, absent, unresolved, cancelled }

request = { key, height }
```

Requirements:

- **The caller owns the list.** The store keeps no queue of its own. This is what
  removes the question of who owns a pending lookup, and with it a whole class of
  bugs where one caller consumes another's results.
- **The result is a partition of the input, decided per request.** Every request
  appears in exactly one of the sets, and never in more than one. A request
  is `absent` only when every version that could still hold it was ruled out with
  certainty; if inspecting any such version failed, **that** request is
  `unresolved`. Authority is per request, not global: one version failing for key
  A must not stop key B being declared absent when everything relevant to B was
  settled.
- **Duplicate keys in a request list are forbidden**, which makes the key itself
  sufficient identity and removes the question of matching repeated requests.
  This is not a simplification for its own sake: no legitimate caller can produce
  one. A batch validation rejects a repeated outpoint through its double-spend set
  before emitting anything; undo capture and deletions iterate a map keyed by
  outpoint; a single transaction's inputs cannot repeat, since that is an internal
  double spend and is rejected earlier. A repeated key *is* a double spend, and
  asking about it twice has no meaning. A debug build may assert on it; a release
  build need not check.

  Given that, the output only has to be matchable by key — the order relative to
  the input is free.
- A read failure must never degrade into absence. It is the same false negative
  as a broken summary, arriving by a different road.
- **Results own their data.** Nothing returned may reference memory the store can
  unmap or evict. Copy before releasing a mapping.
- **One historical visit per request, serving both consumers.** Today the same
  spent outputs are resolved twice per batch: once so the block can be validated,
  and once so its undo data can be captured. They want the entry in different
  shapes — validation wants the materialized output with its height and whether it
  is a coinbase; undo wants the payload exactly as stored, with the height it was
  originally created at, so it can be written straight back on a disconnect. If a
  single resolution can yield both, an entire sweep per batch disappears.

  So a resolved entry has to carry enough for both, owned:

  ```
  resolved_entry {
      request identity
      the stored payload, owned
      the original height
      whether it is a coinbase
      // plus whatever else materializing the output needs
  }
  ```

  The shape is the implementer's to choose. The requirements are that it works in
  both storage modes, that it holds no reference into anything evictable, and
  that one visit answers both needs.

  **Compact mode needs an explicit answer here.** A stored payload there is a
  reference into the block files rather than the entry itself. Is that reference
  stable and owned enough to hand back as-is, or does the entry have to be
  materialized before returning? Either is workable; which one it is changes what
  the node can do with the result and how much a resolution costs.
- Per-version summaries, where the implementation has them, should narrow which
  files this opens: a batch resolution should open only the versions that admit at
  least one of its keys.

Being able to answer many keys in one pass is what makes this affordable, and it
is why the block-connect path benefits far more than any single admission: a
batch resolves hundreds of thousands of keys in one sweep.

### It cannot be an uninterruptible sweep

That same batch is why: hundreds of thousands of keys across many files can take
a long time, and the node has to be able to stop — for shutdown, or because a
reorg needs the state it is reading. Waiting out an unbounded monolithic pass is
not acceptable.

The store does not need to be asynchronous. Any of these serves:

- the operation takes a cancellation signal it polls;
- it is chunked, with a bounded amount of work per call;
- it offers a callback between versions, where stopping is safe;
- it guarantees a maximum latency before a stop request is observed.

Which one is the implementer's choice; that there is one is not.

**And cancelling must not break the partition.** Every request still has to come
back in exactly one place — a request that was never reached cannot simply vanish,
and above all it cannot come back as `absent`, which would be the same false
negative this document spends most of its length preventing, reached by yet
another road.

Cancellation deserves its **own outcome** rather than being folded into
`unresolved`. The two mean different things and the node acts on them
differently: `unresolved` says the store tried and could not read something,
which may be a fault worth reacting to; cancelled says the node itself asked to
stop, which is expected and means only "ask again later". Collapsing them would
repeat exactly the mistake this interface exists to fix, where a read failure
arrived disguised as a missing output.

Cancelling must also leave nothing behind: no retained mapping, no partial state
that a later call inherits.

    resolve_lookups(...) -> { found, absent, unresolved, cancelled }

## 4. Deletes

The same shape, for the same reasons.

```
erase(key, height)                        -> erased | not_in_active_map | error
resolve_deletions(span<request const>)    -> { erased, absent, unresolved, cancelled }
```

`erase` must touch only the active map. Today it also searches cached files
inline, which puts a writer into the file cache from wherever it is called; that
has to go, for the same reason the probe must not touch it.

Deletes are a writer's operation, so they do not need to scale the way probes do.
What they need is to stop being ambiguous: an explicit outcome rather than a
count or a bool that means different things. The partition rules of section 3
apply here too, cancellation included.

## 5. Writes, rotation and compaction

Little to ask for beyond what exists, since these run under exclusion by
construction:

- Applying a delta stays a single operation the node calls once per connected
  block.
- Rotation happens inside insertion. The node's barrier keeps readers out; the
  store does not need to defend itself.
- Compaction stays exclusive with everything.

One request: **rotation and compaction should be observable in cost**, because
the node has to measure the p99 of block connection and the rotation is the whole
tail. A counter of rotations and time spent in them is enough.

**Do not keep the replaced segment mapped as a safety net.** It was considered —
a reader that slipped through a bug in the node's barrier would then read a
stale-but-mapped view rather than unmapped memory — and it is the wrong trade for
this data. A silently old answer about the UTXO set can validate a transaction
against an output that is already spent. A crash is a bug someone investigates; a
stale read is a wrong consensus decision that nothing reports. If a reader gets
through the barrier it should fail loudly, and in debug builds it should be
detected outright (section 7).

## 6. Statistics

They must not stand in the way of the concurrency everything above is for.

- Fixed memory. No per-operation record that accumulates.
- Sharded atomic counters, aligned so shards do not share a cache line — 128
  bytes covers both 64-byte and Apple Silicon 128-byte lines.
- Relaxed ordering where only totals are needed; the summary aggregates the
  shards.
- A probe must never be slower or less concurrent because statistics are enabled.

## 7. Detecting a broken contract

The node owns the barrier, so the node is what can break it. Cheap checks that
turn a silent corruption into a loud failure are worth having in debug builds:

- two historical resolutions running at once — **while the implementation
  declares itself single-executor**. When it advertises concurrent resolution
  this check must go away with the guarantee, not stand as a contradiction of
  it, so the mode has to be something both sides can read;
- a probe running while a mutation is in flight;
- a mutation running while a probe is in flight.

An atomic flag with an assertion is enough. Nothing in release builds.

## 8. What is not being asked for

Stated so it does not get built:

- **No internal locking**, for architectural reasons rather than a technical
  impossibility — process-local mutexes as ordinary members would be perfectly
  implementable. The node already knows the phases and can coordinate better than
  the store can guess; an internal lock would hide serialization where it cannot
  be measured, would stand in the way of batching and of giving a connecting
  block priority, and would duplicate a barrier that has to exist anyway.

  (The separate objection about a lock surviving in a file applies only to locks
  embedded *inside the persisted mapping* — the reason a concurrent container was
  rejected for the active map. It is not an argument against internal
  synchronization in general.)
- **No owner tokens or per-caller queues.** Caller-owned lists make them
  unnecessary.
- **No single-key search across all versions.** It was considered and dropped: it
  would open and unmap files per call. The in-memory absence mechanism plus batch
  resolution replace it.
- **No iterators-only container.** The existing code needs iteration for
  compaction and sweeps.

## 9. Migration

Both interfaces will exist for a while. What the node needs during that:

- The new operations can land before the old ones are removed; the node will
  move one call site at a time.
- While both exist, the old queueing behaviour must stay exactly as it is, so a
  half-migrated node is not half-broken.
- The old operations should be removed once the node stops calling them, not
  deprecated indefinitely. The node is the only consumer.

## 10. Summary of what changes

| | today | needed |
|---|---|---|
| miss on the active map | indistinguishable from absence | its own answer |
| authoritative absence | only after a sweep | from the probe, from memory |
| read failure | indistinguishable from absence | its own answer |
| pending lookups | a queue inside the store | a list the caller owns |
| results | may reference mapped memory | own their data |
| probe concurrency | limited by a shared statistics vector | any number of threads |
| erase | searches cached files inline | active map only |
| resolution | one executor, by construction | one executor by guarantee, not by shape |
