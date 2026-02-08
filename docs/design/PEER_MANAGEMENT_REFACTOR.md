# Peer Management Refactor Plan

**Date:** 2026-02-02
**Status:** In Progress

## Current Architecture (Problems)

### 3 Separate Components

| Component | File | What it stores |
|-----------|------|----------------|
| `banlist` | `banlist.dat` | IP, create_time, ban_until, reason |
| `hosts` | `hosts.cache` | IP:port (known addresses) |
| `peer_session` | (in memory) | `misbehavior_score_` (lost on restart) |

### `peer_provider` in orchestrator.cpp (lines 246-474)

Currently maintains:
- `all_peers` - vector of connected peers
- `header_bad_peers` - set of nonces excluded from header sync
- `peer_performance_stats` - map of nonce → {total_blocks, total_time_ms, sample_count}

**Problems:**
1. Calls `network.ban_peer()` directly (bad design - should only inform peer_manager)
2. Has `is_bannable_error()` inline (should be in peer_manager)
3. Uses hardcoded `max_peers = 32` (should come from settings)
4. All state is lost on restart

### Key Issues

1. **Reputation doesn't persist** - `misbehavior_score` in `peer_session` is lost on restart
2. **`peer_provider` makes ban decisions** - should only report to `peer_manager`
3. **3 files instead of 1** - hard to maintain, inconsistent
4. **Performance stats lost** - must recalculate on every restart
5. **No unified peer database** - can't query "best peers" or "peers to avoid"

## Proposed Design

### Unified Peer Database

Single file `peers.dat` with all peer information:

```cpp
struct peer_record {
    // Identity
    infrastructure::config::authority address;  // IP:port
    std::string user_agent;                     // e.g., "BCHN:28.0.1"
    uint64_t services;                          // NODE_NETWORK, etc.

    // Timestamps
    std::chrono::system_clock::time_point first_seen;
    std::chrono::system_clock::time_point last_seen;
    std::chrono::system_clock::time_point last_attempt;
    std::chrono::system_clock::time_point last_success;

    // Reputation (0 = good, higher = worse, 100+ = ban threshold)
    int reputation_score{0};

    // Ban info (optional - null if not banned)
    std::optional<ban_info> ban;

    // Performance stats
    struct {
        uint64_t total_blocks{0};
        uint64_t total_time_ms{0};
        uint32_t sample_count{0};
        double avg_ms_per_block() const;
    } performance;

    // Connection stats
    uint32_t connection_attempts{0};
    uint32_t connection_successes{0};
    uint32_t connection_failures{0};
};
```

### Refactored `peer_manager`

Move from simple connection tracking to full peer lifecycle management:

```cpp
class peer_manager {
public:
    // === Existing (keep) ===
    awaitable<code> add(peer_session::ptr peer);
    awaitable<void> remove(peer_session::ptr peer);
    awaitable<std::vector<peer_session::ptr>> all() const;

    // === New: Reputation System ===

    /// Report peer behavior (called by peer_provider, protocol handlers, etc.)
    /// Returns action taken (none, warned, banned)
    enum class reputation_action { none, warned, banned };
    reputation_action report_misbehavior(peer_session::ptr peer, int score, std::string_view reason);
    reputation_action report_timeout(peer_session::ptr peer);
    reputation_action report_invalid_data(peer_session::ptr peer, std::string_view reason);
    reputation_action report_slow_peer(peer_session::ptr peer, double avg_ms, double median_ms);

    /// Report good behavior (reduces score over time)
    void report_good_block(peer_session::ptr peer);
    void report_good_headers(peer_session::ptr peer, size_t count);

    // === New: Performance Tracking ===
    void record_block_download(peer_session::ptr peer, uint32_t blocks, uint32_t time_ms);
    void record_header_download(peer_session::ptr peer, uint32_t headers, uint32_t time_ms);

    // === New: Peer Selection ===

    /// Get best peers for sync (excludes banned, slow, misbehaving)
    awaitable<std::vector<peer_session::ptr>> get_sync_peers() const;

    /// Get peers suitable for header sync (stricter filtering)
    awaitable<std::vector<peer_session::ptr>> get_header_peers() const;

    /// Get peers suitable for block sync
    awaitable<std::vector<peer_session::ptr>> get_block_peers() const;

    // === New: Persistence ===
    bool load(kth::path const& file);
    bool save(kth::path const& file) const;

    // === New: Ban Management (moved from banlist) ===
    bool is_banned(asio::ip::address const& ip) const;
    void unban(asio::ip::address const& ip);
    std::vector<peer_record> get_banned_peers() const;

private:
    // Connected peers (existing)
    std::unordered_map<uint64_t, peer_session::ptr> connected_peers_;

    // Peer database (new - all known peers, connected or not)
    boost::concurrent_flat_map<asio::ip::address, peer_record> peer_database_;
};
```

### Refactored `peer_provider`

Becomes a simple "peer distributor" - no decision making:

```cpp
// In orchestrator.cpp peer_provider task:

// BEFORE (bad):
if (is_bannable_error(err->error)) {
    network.ban_peer(err->peer, std::chrono::hours{24 * 365}, ...);
}

// AFTER (good):
auto action = network.peer_manager().report_invalid_data(err->peer, err->error.message());
if (action == reputation_action::banned) {
    // Peer was banned by peer_manager - remove from local lists
    std::erase_if(all_peers, [&](auto const& p) { return p->nonce() == err->peer->nonce(); });
}

// BEFORE (bad):
if ((*it)->misbehave(network::peer_session::misbehavior_slow_peer)) {
    network.ban_peer(*it, std::chrono::hours{1}, ...);
}

// AFTER (good):
auto action = network.peer_manager().report_slow_peer(*it, slowest_avg, median_avg);
// peer_manager decides if ban is warranted
```

### File Format

Simple text format (like current banlist.dat) or JSON for easier debugging:

```
# peers.dat - Knuth peer database
# Format: address first_seen last_seen reputation ban_until services user_agent perf_blocks perf_time_ms perf_samples
192.168.1.1:8333 1706889600 1706976000 0 0 1033 "BCHN:28.0.1" 15000 450000 50
10.0.0.5:8333 1706889600 1706890000 100 1707062400 1033 "ABC:0.27.5" 0 0 0
```

## Migration Plan

### Phase 1: Add peer_record and persistence to peer_manager
1. Add `peer_record` struct
2. Add `peer_database_` concurrent map
3. Add `load()`/`save()` methods
4. Keep existing `banlist` and `hosts` working (backwards compatible)

### Phase 2: Add reputation reporting API
1. Add `report_*` methods to peer_manager
2. Move `is_bannable_error()` to peer_manager
3. Move ban thresholds to peer_manager

### Phase 3: Refactor peer_provider
1. Replace direct `ban_peer()` calls with `report_*` calls
2. Remove local `header_bad_peers` tracking (use peer_manager)
3. Remove local `peer_performance_stats` (use peer_manager)
4. Get `max_peers` from settings

### Phase 4: Deprecate separate files
1. On startup: migrate data from `banlist.dat` and `hosts.cache` to `peers.dat`
2. Remove `banlist` class (functionality in peer_manager)
3. Remove `hosts` class (functionality in peer_manager)
4. Delete old files after successful migration

## Settings Changes

```cpp
// settings.hpp
struct settings {
    // Existing
    kth::path hosts_file{"hosts.cache"};  // DEPRECATED

    // New
    kth::path peers_file{"peers.dat"};
    size_t max_outbound_connections{8};
    size_t max_inbound_connections{24};
    int misbehavior_ban_threshold{100};
    std::chrono::seconds default_ban_duration{24h};
};
```

## Testing

1. Unit tests for `peer_record` serialization
2. Unit tests for reputation scoring
3. Unit tests for peer selection (get_sync_peers, etc.)
4. Integration test: peer misbehavior → ban → persistence → reload → still banned
5. Migration test: old files → new format
