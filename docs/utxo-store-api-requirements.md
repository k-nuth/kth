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
| historical resolution | **must support concurrent execution** — see section 3 |
| insert, erase, delta application | one writer, no readers |
| rotation | one writer, no readers (it happens inside insert) |
| compaction | exclusive with everything |
| size, statistics | must not require exclusion against probes |

**Historical resolution is the main ask, and this is a change from the first
version of this document.** It was written as "keep the shape from precluding
concurrency later". That was wrong about where the cost is, and the analysis that
corrected it is worth stating because it changes what gets built:

- a transaction spending recent outputs resolves from the active map already,
  concurrently, today. Nothing needs adding for that case.
- a transaction spending an **older but live** output goes to historical
  resolution — and no in-memory absence summary changes that, because the summary
  can only say "maybe", never "here it is".
- an outpoint that exists nowhere is the only case an absence summary settles.

The active map holds a bounded number of entries before it rotates. As the live
set outgrows the active maps, an increasing — and currently unmeasured — share of
valid spends may require historical resolution. So **the path that has to scale
for valid traffic is historical resolution**, and it is the one that is serialized
today.

How large that share actually is, is a measurement rather than a claim, and it is
one the statistics rework has to make possible.

That makes it the central requirement rather than a door left open.

## 2. Probing the active map

The single most important operation, because it is the one every admission runs
and the one that must scale.

Two signatures, because the modes hold different things and pretending otherwise
would cost a copy in one of them.

**Full mode** — the value lives in the mapped segment, so it is written straight
into storage the caller supplies:

```
probe(request, buffer_provider)
    -> found(bytes_written, creation_height)
     | absent
     | needs_resolution
     | error

buffer_provider(required_size) -> span<uint8_t>
```

**Compact mode** — the entry is a small POD the store holds by value, so there is
nothing to lend:

```
probe(request)
    -> found(compact_find_result)
     | absent
     | needs_resolution
     | error
```

Requirements common to both:

- `const`, and safe from any number of threads at once.
- Queues nothing. Keeps no per-caller state. Has no side effects a later call
  can observe.
- Touches no file cache and opens no file.
- Never blocks on another caller.

And of the provider, in full mode:

- **`noexcept` and non-reentrant.** It reserves space in caller-owned storage and
  does nothing else.
- **Called only after an eligible hit is found.** A hit that is too new for the
  request's bound returns `needs_resolution` **without calling it** — no space is
  reserved for an answer that is not being given.
- **A null span, or one smaller than `required_size`, is
  `output_buffer_unavailable`** — never `needs_resolution`. Confusing "you gave me
  nowhere to put it" with "look in history" would send the caller down the
  serialized path for a bug of its own.
- it returns a plain span, not an expected: **the reason a caller could not
  provide space belongs to that caller's own synchronous context**, where it
  already is. Threading it back out through the store would carry it in a circle.

**`output_buffer_unavailable` is the caller's own failure**, and the node's
handling of it is constrained even though the choice is the node's:

- the transaction is **not** classified invalid;
- it is **not** read as a missing prevout;
- it is **not** attributed to a peer;
- **no negative decision is published** on the strength of it.

Whether it is fatal, retryable or a signal to apply backpressure depends on why
the space could not be provided — which the provider knows and the store never
will. The caller keeps that cause in its own context and decides there.

One copy, one lookup, and no worst-case reservation — the caller learns the size
it needs at the moment it needs it.

### The four answers

Today's interface can only say "in the active map" or "not in the active map",
and the second is reported with the same code that means "does not exist". That
is the direct cause of the node rejecting valid transactions: a miss reads as
absence.

The four answers:

- **found** — an eligible entry is in the active map. Authoritative.
- **needs_resolution** — the active map cannot settle it. The caller collects
  these for section 3. **This is the answer the first
  implementation gives for every active-map miss, and for every hit too new for
  the request's bound — everything, in short, that needs historical resolution.**
  Failures are `error`, not this.
- **absent** — provably not anywhere. Authoritative, reached without opening
  anything. See below: this is optional.
- **error** — the store could not answer. Never to be conflated with `absent`.

### `absent` from the probe is optional, and stays in the contract anyway

The first version of this document made authoritative absence from the probe a
requirement, on the reasoning that a probe which can only say "I don't know"
forces every miss down the serialized path. The second half of that is true; the
conclusion was not.

**Fixing #584 does not need it.** The minimum correct behaviour is:

```
active-map miss     -> needs_resolution
historical resolve  -> found | absent | unresolved | cancelled
```

Authoritative absence comes from the resolution, which is where it can be
established. The probe never has to claim it.

So `absent` stays in the enumeration — so that adding the capability later is not
an API change — but **the first implementation may never return it**, and nothing
should be built to make it possible until measurement says the cost is worth it.

**If it is ever produced, the integrity conditions below become mandatory again**,
in full. They are not softened by being deferred; they are the price of the
capability, payable when it is taken up.

### What it would buy, stated accurately

Not throughput for valid traffic. A transaction spending a live older output gets
`needs_resolution` either way — a summary can only rule a key *out*, and that key
is in there somewhere. What it settles is the third case: an outpoint that exists
nowhere, rejected from memory and concurrently.

That is protection against having work extracted from the node, not scaling. It
matters, and section 3's note on the amplification explains why it is on the
roadmap rather than dismissed — but it is not the piece that makes admission
scale.

### `max_creation_height`, and what it does not mean

The lookups take a height. Its current meaning is documented as "statistics only;
does not affect the result" — the bound that matters is applied afterwards by the
node, turning a too-new entry back into a miss.

It should become a real bound, moved inside, with this contract:

- an entry is eligible only if `creation_height <= max_creation_height`;
- **finding a too-new entry does not authorize `absent`.** The resolution must
  keep looking for an eligible historical entry;
- `absent` means "no live eligible entry exists under that bound" — **not** "the
  UTXO set looked like this at that height";
- the version-to-height relationship is **not** used to decide which files to
  skip.

The name matters as much as the rule. The store can hide entries created above
the bound; it cannot resurrect entries spent between the bound and the tip. So
this is *the current set minus what was created later*, which is exactly what the
node computes outside today. Anything named for a historical view would be a
wrong answer waiting to be believed.

Two consequences worth stating plainly:

**The second rule is a behaviour change, not a translation.** Today the node finds
the entry, sees it is too new, and answers miss without looking further. Keeping
the search going is a correction, and it is what protects the repeated-outpoint
cases: a newer entry must not hide an older eligible one.

**Skipping files by height is not implementable**, and this is settled rather than
open. Compaction drains newer versions into older files, and a reorg's undo
reinserts at the height the entry was originally created at, so the correlation
breaks from both ends. The metadata that would express it is unreliable for
absent versions besides. Using it to skip a file is precisely the false negative
this document exists to prevent.

### The requirement, stated without fixing the mechanism

If the capability is taken up, what it has to satisfy is this, and nothing
narrower:

> The probe must be able to **prove absence from memory, at bounded cost, without
> opening a version file.**

Per-version summaries are the obvious way, but walking one per historical version
is O(versions) per miss, which is the wrong shape once versions accumulate. A
hierarchical index, an aggregate summary backed by per-version ones, or something
else entirely are all acceptable if they meet the requirement. Per-version
summaries remain useful regardless, for deciding which files a batch resolution
opens — that is a separate use with no correctness weight.

### Integrity, if and when `absent` is produced

Deferred, not weakened. The moment a probe can answer `absent`, whatever it
consulted becomes part of the node's correctness boundary: a wrong `absent`
rejects a valid transaction, or worse, in a path that trusts it. Everything in
this section is a condition of taking up that capability, and none of it is
optional once it is taken up.

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

## 3. Historical resolution

For the keys a probe could not settle.

```
borrowed_entry {
    borrowed_payload payload
    creation_height
}

resolve_lookups(span<request const>, sink)
    -> { found_count, absent, unresolved, cancelled }

sink(request const&, borrowed_entry) -> void

request = { key, height }
```

The entry carries the **creation height**, and it has to: `request.height` is the
bound the lookup was made under, not the height the entry was created at. Undo
needs the second so it can write the value back as it was. They are different
numbers and only one of them is in the request.

**Results are delivered rather than returned**, and that is a change from an
earlier draft which asked for owned results. The node has to hold the prevouts
anyway — validation and undo capture both read them — so a store that returned
owned entries would produce a second copy of everything, only for the node to
copy out of it. Handing a borrow to a callback removes that copy and leaves the
node with the single one it needs.

- the borrow is valid **only for that sink invocation** — not for the call to
  `resolve_lookups` that contains it — and no longer. The handle is
  alive for it; nothing survives it.
- the sink is **`noexcept`, enforced at compile time**. A throwing sink in the
  middle of a mapped walk has no good answer.
- the sink is **not reentrant into the store**, and the reason is worth stating
  precisely: not because an administrative mutex is held, but because of the
  phase discipline and the one-handle-per-executor rule. Calling back in would
  break both.
- **the sink copies into caller-owned state and returns. That is all it does.**
  No I/O, no materializing a compact reference, no parsing, no blocking work.
  The node materializes and parses **after the handle is released**, in both
  modes — a compact reference costs a block-file read and a whole transaction
  parse to materialize, and doing that inside the callback would hold a version
  mapping across I/O that has nothing to do with it. Full mode defers its parse
  for the same reason, minus the I/O: the lease stays short.

  **Deferring also fixes something, and that is worth keeping.** Today a compact
  materialization that fails — an unreadable block file, an unparseable
  transaction — puts the key in the failed set, where the caller reads it as a
  missing prevout and therefore as an invalid block: a local I/O fault dressed up
  as a peer's fault. With materialization moved out, the resolution answers
  `found` and a later failure is unambiguously local, with nothing to say about
  whether the output exists. Anyone reintroducing materialization inside the sink
  "to change less" would be putting that conflation back.
- **every invocation is one found request.** There is no return value and no way
  to stop part-way: no caller wants one. Batch validation, undo capture and
  admission each need every key, and shutdown or a reorg cancel through the
  external token rather than through the sink.
- **no request id.** The `request const&` handed to the sink carries the key and
  the height, which is all the identity the forbidden-duplicates rule leaves
  needed.
- if the caller cannot store what it is handed, **that is the caller's failure**.
  It is not reported as `cancelled` or `unresolved`, which mean something the
  store observed.
- the store keeps `O(1)` of its own beyond the requests and their classification.
- an **owning variant** may be built on top for convenience. It does not
  materialize inside the sink either.

The other requirements:

- **The caller owns the list.** The store keeps no queue of its own. This is what
  removes the question of who owns a pending lookup, and with it a whole class of
  bugs where one caller consumes another's results.
- **The result is a partition of the input, decided per request** — now verified
  by `found_count` against the three classified sets rather than by a fourth set
  of identities, which would duplicate what the sink already delivered. Every
  request lands in exactly one place and never in more than one. A request is
  `absent` only when every version that could still hold it was ruled out with
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
- A read failure must never degrade into absence. It is the same false negative
  as a broken summary, arriving by a different road.
- **The creation-height bound applies per version, not once.** Section 2 states
  it under the probe, but the rule that matters is the resolver's: finding an
  entry for a key in some version is not the end of that key. If it is too new
  for the request's bound it is **not** `found`, and it does **not** authorize
  `absent` — the walk continues into older versions looking for an eligible copy.

  This is worth repeating here because the natural way to write a sweep is "find
  the key in this file, deliver it, done with that key", and that would answer
  `absent` for an output that exists and is eligible. It is the same class of
  defect as reading a miss as absence, one level down.
- **Batch size is the node's knob and the node's budget.** The store does not
  decide how much work a call represents.
- An **owning variant** may be built on top for callers that want one. It is
  expressly not the batch path, and the batch path does not grow a second copy to
  provide it.

### One historical visit, serving both consumers

Today the same spent outputs are resolved twice per batch: once so the block can
be validated, and once so its undo data can be captured. The two want the entry
in different shapes — a materialized output for validation, the payload exactly
as stored for undo — but they come from the same visit, so one sweep per batch
disappears when the sink hands over the stored payload and the creation height
and lets each consumer take what it needs.

What the sink delivers is therefore:

```
request identity   (key, height)  — from the request reference
the stored payload — lent, valid only for this sink invocation
the original creation height
```

**And nothing else.** An earlier draft asked for a coinbase flag; that was the
node asking for something it already has, and it is withdrawn. In full mode the
node writes the coinbase byte and the median time past *inside the value*, so the
payload carries them and the store stays agnostic to the content. In compact mode
the node derives them while materializing the transaction it must parse anyway.
Neither needs a format change nor a bit stolen from the height.

**The payload's shape follows the storage mode, and that is fine.** In compact
mode it is the small POD reference the store holds — the store does not have the
output's bytes and cannot materialize it. In full mode it is the stored value.
One shape does not have to serve both.

### It has to run concurrently, and that is the central ask

Everything above removes the coupling *between callers* — no shared queue, lists
the caller owns, borrows that do not outlive their invocation. That is
necessary and not
sufficient: underneath, version files are reached through a cache that can unmap
one while a thread is reading it, so concurrency is blocked by the implementation
rather than by the interface.

This is the piece that decides whether admission scales for valid transactions
spending older outputs. It is worth being precise that it is **three distinct
problems**, not one, and a proposal can solve one and leave the others open:

1. **Catalogue stability** — the set of versions a resolution is working against
   must not shift under it while it runs.
2. **Mapping lifetime** — a mapping must stay alive until its last reader is
   done, and no eviction may cut that short.
3. **Cache administration** — the bookkeeping around all of that must not
   serialize every resolution, or the concurrency is nominal.

A catalogue snapshot answers the first and does nothing about the second. Handing
each resolution its own mapping answers the second at a memory cost. They compose;
they do not substitute.

### The mapping registry, as agreed

No longer an open comparison — the store's side answered it and this records the
shape settled on, so implementation and review work from the same text.

**Deduplication is global, through a registry**, keyed by identity:

```
(container, version, catalogue_generation)
```

The generation keeps an identity from being reused across catalogues. It does
**not** replace the external barrier — it makes stale identities detectable, not
concurrent mutation safe.

**The registry belongs to a database instance**, and "global" means shared by
every path and executor of that database. The key above identifies a file within
one database and nothing more, so a process-wide registry would let two databases
collide on it. Owning it per instance keeps the key as it is, rather than
threading a path or an instance identifier through every lookup.

**A handle owns its mapping.** A resolution holds handles for what it is reading;
a mapping outlives every handle to it. Cancelling or unwinding releases them
through RAII, so no path can leak retention.

**No I/O under the administrative mutex.** The mutex covers the registry's
bookkeeping and nothing else. Opening and mapping happen outside it, or the
serialization the registry exists to avoid comes back one level down.

**Single-flight per identity.** Two resolutions wanting the same version do not
open it twice: one becomes the owner and opens, the others wait. A failed open is
published to the waiters **and withdrawn from the registry**, so the next attempt
is a fresh one rather than a cached failure.

**Waiters wait outside the mutex, and may cancel.** Waiting on a future is not
holding the registry.

**Deduplication is independent of LRU promotion.** They are separate policies:
sharing a mapping does not imply it should be retained. A scan can deduplicate
without polluting the cache with versions it will never revisit.

**Exclusive operations invalidate explicitly**, and refuse to proceed if anything
still holds what they are invalidating. Compaction and the rest do not rely on
entries ageing out; they say what is no longer valid — and if a read lease exists
for the generation being invalidated, the operation must **fail loudly rather
than wait or continue**, in release builds and not only under an assertion.

Under the node's barrier that situation cannot arise, which is exactly why it has
to be detected: if it does arise, the barrier has been violated and that is the
node's bug. Handles physically prevent unmapping under a reader, but nothing
prevents a writer publishing a new catalogue while an older reader is still
working. The generation lets that reader's result be rejected *afterwards*; the
reader's presence proves the violation *beforehand*, which is when it is worth
knowing.

**Resources are bounded as a whole**, and the bound rests on a discipline that has
to be stated with it:

> Each executor or scan holds **at most one version handle at a time**: resolve
> every applicable pending key against it, deliver each to the sink, release the
> handle, then move to the next version.

Without that rule a single resolution can retain every version it walks, and the
bound below is not a bound at all. With it:

```
live mappings <= cache capacity + active executors + active scans
```

with configuration and compaction accounted separately, so a maintenance
operation is not mistaken for load. Expired weak entries are pruned, so the
registry's own bookkeeping does not grow without limit either.

**Resolutions read through read-only mappings.** Read-write mappings exist only
for the exclusive phases — deferred deletions and compaction — and they are
scoped to that phase and not retained afterwards.

That gives the layer its own job: a reader physically cannot write. An errant
write from a resolution becomes a compile error or a fault rather than silent
corruption of a file that cannot be rebuilt.

**The mode is not part of the identity**, because the two never coexist for one
identity. That makes the transition the thing to get right, and it has to be
symmetric:

- **before acquiring read-write**: invalidate the read-only mappings for that
  identity, and require **zero read leases**;
- **after the exclusive phase**: release the read-write mapping and invalidate it
  before any new reader is admitted. It is **not retained and not promoted** in
  the cache, and a later read-only acquisition must never recover it from the weak
  registry.

In short, read-write is scoped and deduplication-only; read-only is what the cache
is for.

Read-only is **the agreed default**, not a decision still open. What the
benchmark settles is whether to keep it, **with the threshold fixed before
measuring** so the number cannot be chosen to fit the result: reconsider if the
read-only → read-write → read-only cycle costs more than **5% of throughput or of
p99** against a single read-write mapping. What to record: versions touched, batch
size, key dispersion, opens, minor faults, live mappings and file descriptors.

### Lease conflict is an error, not an abort

The check happens **when read-write is acquired for a concrete identity**, not by
predicting at the start which files a deletion pass will touch. Prediction would
be both unreliable and pointlessly conservative.

A conflict returns **`lease_conflict`**. The library does not abort: the owner
keeps its lifecycle, and the condition is testable. **The node treats it as
fatal** — under its barrier a live read lease during an exclusive phase means the
barrier was violated, which is a defect in the node, not a condition to recover
from.

For that to be expressible, **the `void` operations return `result<>`**,
`compact_all()` included. An operation that can refuse has to be able to say so.

### How the layers relate

Four mechanisms, none of which replaces another:

| layer | what it does |
|---|---|
| the node's barrier | prevents the overlap in the first place |
| read leases | enforce the precondition in release, so a violation is caught before state changes |
| read-only mappings | turn an accidental write from a reader into a fault or a compile error |
| the catalogue generation | prevents identity reuse across catalogues, and lets a stale result be rejected |

Worth being exact about one thing, because it is easy to state too strongly:
**handles do not by themselves turn a crash into a silent stale read.** Under the
full contract the release lease check stops the writer from starting, so no new
state is published while a reader exists. A stale read only becomes possible if
that check is also missing or skipped. The layers are complementary, and the
argument for each is its own.

### It cannot be an uninterruptible sweep

That same batch is why: hundreds of thousands of keys across many files can take
a long time, and the node has to be able to stop — for shutdown, or because a
reorg needs the state it is reading. Waiting out an unbounded monolithic pass is
not acceptable.

The store does not need to be asynchronous, and it cannot promise a maximum
latency — an open and an mmap take as long as they take, and no wall-clock bound
survives contact with a cold file. So the contract is written in units of work
rather than of time:

- **cancellation is observed between opens, and within a version's walk.** Those
  are the granularities, and they are the honest ones.
- **chunks are bounded by request count or by bytes delivered**, whichever comes
  first. Both bounds are internal and tunable; neither is a promise about time.
- **an open, once its owner has started it, is not interruptible.** It runs to
  completion.
- **waiters may abandon** the future they are waiting on and return their requests
  as `cancelled`, without waiting for the owner.
- so there may be **one uninterruptible owner per file in flight**, never one
  globally. A cancelling caller waits for at most the opens it started itself.
- **cancelling releases every handle held**, through RAII rather than through an
  unwinding path that has to remember.
- what was already delivered counts in `found_count`; what was not becomes
  `cancelled`. Cancelling does not retract.

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

    resolve_lookups(...) -> { found_count, absent, unresolved, cancelled }

### One consequence the node owns

Recorded here because it follows from this interface and it would be easy to leave
implicit. Today a transaction whose prevout does not exist is rejected **without
historical I/O** — the node probes, queues the deferred lookup, reads the miss as
absence and stops. That is the bug. Once a miss goes to resolution, that same
traffic opens files.

So the correctness fix opens an amplification the node has to mitigate — by
batching admission resolutions, by limiting how much resolution admission can
cause, or by spending one only after the cheap checks pass. It is the node's
problem, not the store's, and it is being handled alongside the fix. It appears
here only to explain why an in-memory absence mechanism stays on the roadmap
after being made optional.

## 4. Deletes

The same shape, for the same reasons.

```
erase(key, height)                        -> erased | not_in_active_map | error
resolve_deletions(span<request const>)    -> { erased_count, absent, unresolved, cancelled }
```

`erase` must touch only the active map. Today it also searches cached files
inline, which puts a writer into the file cache from wherever it is called; that
has to go, for the same reason the probe must not touch it.

Deletes are a writer's operation, so they do not need to scale the way probes do.
What they need is to stop being ambiguous: `erase` returns an explicit outcome
rather than a count or a bool that means different things, while the batch result
uses `erased_count` without duplicating the erased request identities. The caller
already owns the input span, and the count together with the other three sets is
enough to verify the partition. The partition rules of section 3 apply here too,
cancellation included.

## 5. Writes, rotation and compaction

Little to ask for beyond what exists, since these run under exclusion by
construction:

- Applying a delta stays a single operation the node calls once per connected
  block.
- Rotation happens inside insertion. The node's barrier keeps readers out; the
  store does not need to defend itself.
- Compaction stays exclusive with everything, and returns `result<>` like the
  other operations that can refuse (section 3).

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

**Lease metrics specifically**, because they are what says whether the read-only
default is paying for itself:

- how long leases are held, in total and at the maximum;
- requests and bytes delivered;
- duration normalized per request and per byte, which is what makes runs with
  different batch sizes comparable;
- sharded, or with no shared counters at all on the concurrent path — the
  measurement must not become the contention it is measuring;
- sampling of callbacks only if diagnosis later needs it, not by default.

## 7. Detecting a broken contract

The node owns the barrier, so the node is what can break it. Cheap checks that
turn a silent corruption into a loud failure are worth having in debug builds:

Two different things, with different costs and different lifetimes.

**Enforced in release, not only asserted:** a mutation of mapped data must not
begin while read leases are alive for what it is about to touch. That is not
instrumentation, it is a precondition — the operation returns `lease_conflict`
rather than waiting or continuing, and the node treats that as fatal. See section 3 for why the reader's presence is worth
knowing before state changes rather than after.

It applies to **every** operation that may mutate mapped data, not only to
replacing a catalogue or invalidating a generation. Historical deletion
processing is the case to keep in mind: it can mutate a version file without its
identity changing at all, so there is no generation bump to hang the check on, and
overlapping a resolver is a race regardless.

The lease must be **an explicit counter or a distinguishable type**. Do not infer
readers from a shared pointer's use count: the cache and the registry hold
administrative references of their own, so a count that mixes them cannot answer
"is anyone reading this".

**Debug-only instrumentation**, cheap and removable:

- two historical resolutions running at once — **while the implementation
  declares itself single-executor**. When it advertises concurrent resolution
  this check goes away with the guarantee rather than standing as a contradiction
  of it, so the mode has to be something both sides can read;
- a probe running while a mutation is in flight;
- a mutation running while a probe is in flight.

An atomic flag with an assertion is enough for those.

## 8. What is not being asked for

Stated so it does not get built:

- **No internal reader/writer phase barrier.** What is not wanted is the store
  deciding when readers and writers may run — the node already knows the phases
  and can coordinate better than the store can guess; an internal barrier would
  hide serialization where it cannot be measured, stand in the way of batching and
  of giving a connecting block priority, and duplicate something that has to exist
  anyway.

  **This is not "no mutexes".** The registry in section 3 requires a short
  administrative mutex, and that is part of the design: it covers the registry's
  own bookkeeping, never I/O and never a read of mapped data. The distinction is
  between synchronizing a small internal structure and taking over the phase
  discipline.

  (The separate objection about a lock surviving in a file applies only to locks
  embedded *inside the persisted mapping* — the reason a concurrent container was
  rejected for the active map. It is not an argument against internal
  synchronization in general.)
- **No lookup-owner tokens for routing a shared pending queue.** Caller-owned
  request lists make those unnecessary. Mapping-lifetime handles are a different
  thing and are a requirement — see section 3.
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
| the height argument | statistics only | a creation-height bound, with a too-new entry never yielding absence |
| authoritative absence | not expressible at all | from the resolution; from the probe only if the capability is taken up |
| read failure | indistinguishable from absence | its own answer |
| pending lookups | a queue inside the store | a list the caller owns |
| results | may reference mapped memory | delivered to a sink, valid for that invocation; probe writes into caller storage in full mode, returns a POD in compact |
| probe concurrency | limited by a shared statistics vector | any number of threads |
| erase | searches cached files inline | active map only |
| resolution | one executor, by construction | **concurrent** — the central requirement |
| mappings for readers | read-write, like everything | read-only; read-write is scoped to the exclusive phases |
