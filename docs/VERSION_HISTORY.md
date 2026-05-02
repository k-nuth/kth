# Version History & Repository Transition

Knuth started life as a constellation of small repositories, one per
component (node, c-api, blockchain, …), each with its own version
number and release schedule. **Starting with v0.68.0 (June 2025)** the
project consolidated into this single repository, with one
synchronized version that covers every component.

> **A note on repo names.** The mono-repo originally launched as
> `k-nuth/kth-mono` while the legacy multi-repo lived at `k-nuth/kth`.
> Those have since been swapped: the mono-repo is now `k-nuth/kth`
> (this one), and the legacy multi-repo is `k-nuth/kth-legacy`. Older
> URLs that still reference `kth-mono` redirect for now but should be
> updated when you spot them.

## Repository architecture

| Period | Layout | Versioning |
|---|---|---|
| **Before v0.68.0** | One repo per component | Independent (e.g. C-API at v0.67.0 alongside Node at v0.58.0) |
| **From v0.68.0 onwards** | This mono-repository | One version, all components |

The split before v0.68.0 was occasionally surprising — some components
released frequently while others remained stable for long stretches,
so version numbers diverged. The mono-repo collapses that into a
single release line.

## The v0.59.0 – v0.67.0 gap on Node and Node-exe

Node and Node-exe appear to skip from v0.58.0 straight to v0.68.0.
Nothing was lost: during 2024–2025 the C-API was the active surface
and shipped 0.59.0 → 0.67.0 with regular updates, while Node and
Node-exe stayed at v0.58.0 because no consumer-visible changes warranted
a new release. When the mono-repo cut its first release at v0.68.0,
Node simply jumped to match the unified line.

## Where to find pre-mono releases

| Component | Version range | Repository |
|---|---|---|
| C-API | 0.47.0 → 0.67.0 | [`k-nuth/c-api`](https://github.com/k-nuth/c-api/releases) |
| Node | 0.47.0 → 0.58.0 | [`k-nuth/node`](https://github.com/k-nuth/node/releases) |
| Node-exe | 0.47.0 → 0.58.0 | [`k-nuth/node-exe`](https://github.com/k-nuth/node-exe/releases) |
| Everything from 0.68.0 onwards | unified | [`k-nuth/kth`](https://github.com/k-nuth/kth/releases) (this repo) |

## Why the consolidation

- **One release process** instead of N coordinating release cycles.
- **One version everywhere** — no compatibility matrix between
  C-API 0.65.x and Node 0.57.x.
- **One codebase, one build system** for contributors.
- **Atomic cross-component changes**: a change touching node + c-api +
  domain lands in one commit and ships in one release, not three.
