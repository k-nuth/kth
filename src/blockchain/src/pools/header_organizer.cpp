// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/header_organizer.hpp>

#include <algorithm>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/utility/system_memory.hpp>

namespace kth::blockchain {

using namespace kth::domain::chain;

// =============================================================================
// Construction / Destruction
// =============================================================================

header_organizer::header_organizer(block_chain& chain, settings const& settings,
                                   domain::config::network network,
                                   header_cache_config config)
    : chain_(chain)
    , validator_(settings, network)
    , config_(config)
{}

header_organizer::~header_organizer() {
    // Attempt to flush any pending headers on destruction
    if (has_pending()) {
        spdlog::warn("[header_organizer] Destroying with {} unflushed headers",
            header_cache_.size());
    }
}

// =============================================================================
// Lifecycle
// =============================================================================

bool header_organizer::start() {
    stopped_ = false;
    return true;
}

bool header_organizer::stop() {
    stopped_ = true;
    return true;
}

// =============================================================================
// Initialization
// =============================================================================

void header_organizer::initialize(size_t current_height, hash_digest const& current_tip_hash,
                                  size_t expected_headers) {
    std::lock_guard lock(mutex_);

    db_header_height_ = current_height;
    db_tip_hash_ = current_tip_hash;
    cache_tip_hash_ = current_tip_hash;

    // Pre-allocate cache if we know how many headers to expect
    if (expected_headers > 0) {
        auto const optimal_size = calculate_optimal_cache_size(expected_headers);
        header_cache_.reserve(optimal_size);
        hash_cache_.reserve(optimal_size);

        spdlog::info("[header_organizer] Initialized: DB height {}, cache pre-allocated for {} headers",
            current_height, optimal_size);
    } else {
        spdlog::info("[header_organizer] Initialized: DB height {}", current_height);
    }
}

// =============================================================================
// Header Addition
// =============================================================================

header_organize_result header_organizer::add_headers(domain::message::header::list const& headers) {
    header_organize_result result;

    if (stopped()) {
        result.error = error::service_stopped;
        return result;
    }

    if (headers.empty()) {
        result.error = error::success;
        return result;
    }

    std::lock_guard lock(mutex_);

    spdlog::debug("[header_organizer] add_headers() called with {} headers", headers.size());

    // Current tip is either from cache or from DB
    hash_digest prev_hash = cache_tip_hash_;
    size_t height = db_header_height_ + header_cache_.size() + 1;

    spdlog::debug("[header_organizer] Starting validation loop at height {}, cache_tip_hash: {}",
        height, encode_hash(cache_tip_hash_));

    if (!headers.empty()) {
        spdlog::debug("[header_organizer] First header previous_block_hash: {}",
            encode_hash(headers.front().previous_block_hash()));
    }

    for (auto const& header : headers) {
        // Validate header
        auto const ec = validate(header, height, prev_hash);
        if (ec) {
            spdlog::debug("[header_organizer] Header validation failed at height {}: {}",
                height, ec.message());
            result.error = ec;
            break;
        }

        // Compute hash and add to caches
        auto const hash = header.hash();
        header_cache_.push_back(header);
        hash_cache_.push_back(hash);

        prev_hash = hash;
        cache_tip_hash_ = hash;
        ++height;
        ++result.headers_added;

        // Log progress every 500 headers
        if (result.headers_added % 500 == 0) {
            spdlog::debug("[header_organizer] Validated {} headers...", result.headers_added);
        }
    }

    spdlog::debug("[header_organizer] Validation complete: {} headers added", result.headers_added);

    result.cache_size = header_cache_.size();
    result.cache_memory_bytes = cache_memory_impl();

    // Check if we should auto-flush
    if (should_auto_flush()) {
        spdlog::info("[header_organizer] Cache threshold reached ({} headers, {} MB), auto-flushing",
            header_cache_.size(),
            result.cache_memory_bytes / (1024 * 1024));

        auto const flush_ec = flush_impl();  // Use _impl to avoid deadlock (we already hold mutex_)
        if (flush_ec && result.error == error::success) {
            result.error = flush_ec;
        }
    }

    return result;
}

// =============================================================================
// Flush to Database
// =============================================================================

code header_organizer::flush() {
    std::lock_guard lock(mutex_);
    return flush_impl();
}

// Internal version without lock (caller must hold mutex_)
code header_organizer::flush_impl() {
    if (header_cache_.empty()) {
        return error::success;
    }

    auto const start_height = db_header_height_ + 1;
    auto const count = header_cache_.size();

    spdlog::debug("[header_organizer] Flushing {} headers to DB starting at height {}",
        count, start_height);

    // Store all headers in batch
    auto const ec = chain_.organize_headers_batch(header_cache_, start_height);
    if (ec && ec != error::duplicate_block) {
        spdlog::warn("[header_organizer] Failed to flush headers batch: {}", ec.message());
        return ec;
    }

    // Update DB state
    db_header_height_ = start_height + count - 1;
    db_tip_hash_ = cache_tip_hash_;

    // Clear cache
    header_cache_.clear();
    hash_cache_.clear();

    spdlog::debug("[header_organizer] Flush complete, DB height now {}", db_header_height_);

    return error::success;
}

// =============================================================================
// Cache Access
// =============================================================================

std::vector<hash_digest> header_organizer::get_cached_hashes() const {
    std::lock_guard lock(mutex_);
    return hash_cache_;
}

void header_organizer::clear_cache() {
    std::lock_guard lock(mutex_);

    header_cache_.clear();
    header_cache_.shrink_to_fit();
    hash_cache_.clear();
    hash_cache_.shrink_to_fit();

    // Reset cache tip to DB tip
    cache_tip_hash_ = db_tip_hash_;
}

// =============================================================================
// State Queries
// =============================================================================

size_t header_organizer::header_height() const {
    std::lock_guard lock(mutex_);
    return db_header_height_ + header_cache_.size();
}

hash_digest header_organizer::header_tip_hash() const {
    std::lock_guard lock(mutex_);
    return cache_tip_hash_;
}

size_t header_organizer::cache_size() const {
    std::lock_guard lock(mutex_);
    return header_cache_.size();
}

size_t header_organizer::cache_memory() const {
    std::lock_guard lock(mutex_);
    return cache_memory_impl();
}

// Internal version without lock (caller must hold mutex_)
size_t header_organizer::cache_memory_impl() const {
    auto const header_size = estimated_header_size();
    auto const hash_size = sizeof(hash_digest);
    return static_cast<size_t>(
        (header_cache_.size() * header_size + hash_cache_.size() * hash_size)
        * config_.memory_overhead
    );
}

bool header_organizer::has_pending() const {
    std::lock_guard lock(mutex_);
    return !header_cache_.empty();
}

// =============================================================================
// Memory Estimation
// =============================================================================

size_t header_organizer::calculate_optimal_cache_size(size_t items_needed) const {
    // Header size + hash size per item
    auto const item_size = estimated_header_size() + sizeof(hash_digest);

    auto const available = kth::get_available_system_memory();
    if (available == 0) {
        // Fallback: use config max or reasonable default
        auto const max_items = config_.max_cache_memory / item_size;
        return std::min(items_needed, max_items);
    }

    // Memory per item including overhead
    auto const bytes_per_item = static_cast<size_t>(item_size * config_.memory_overhead);

    // Use configured max or fraction of available memory (whichever is smaller)
    auto const memory_fraction = available / 2;  // Use at most 50% of available
    auto const usable_memory = std::min(config_.max_cache_memory, memory_fraction);

    // Calculate how many items we can fit
    auto const max_items = bytes_per_item > 0 ? usable_memory / bytes_per_item : 0;

    return std::min(items_needed, max_items);
}

size_t header_organizer::estimated_header_size() {
    // Header base size: version(4) + prev_hash(32) + merkle(32) + time(4) + bits(4) + nonce(4) = 80 bytes
    // Plus std::vector overhead and alignment
    return 80 + 48;  // ~128 bytes estimated with overhead
}

// =============================================================================
// Internal Helpers
// =============================================================================

code header_organizer::validate(domain::chain::header const& header, size_t height,
                                hash_digest const& previous) const {
    return validator_.validate(header, height, previous);
}

bool header_organizer::should_auto_flush() const {
    // Note: caller must hold mutex_
    auto const current_memory = static_cast<size_t>(
        (header_cache_.size() * estimated_header_size() + hash_cache_.size() * sizeof(hash_digest))
        * config_.memory_overhead
    );

    auto const threshold = static_cast<size_t>(config_.max_cache_memory * config_.auto_flush_threshold);
    return current_memory >= threshold;
}

} // namespace kth::blockchain
