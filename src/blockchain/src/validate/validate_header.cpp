// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_header.hpp>

#include <algorithm>

#include <spdlog/spdlog.h>

#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace kth::domain::chain;
using namespace kth::infrastructure::config;

validate_header::validate_header(settings const& settings, domain::config::network network)
    : checkpoints_(checkpoint::sort(settings.checkpoints))
    , network_(network)
    , retarget_(network != domain::config::network::regtest)
{}

// Context-free validation
// -----------------------------------------------------------------------------

code validate_header::check(domain::chain::header const& header) const {
    // Delegate to header's check() which validates:
    // 1. Proof of work (hash <= target derived from bits)
    // 2. Timestamp not too far in future (2 hours)
    return header.check(retarget_);
}

// Chain-context validation
// -----------------------------------------------------------------------------

code validate_header::accept(domain::chain::header const& header, size_t height,
                             hash_digest const& previous) const {
    // Validate chain continuity
    if (header.previous_block_hash() != previous) {
        return error::store_block_missing_parent;
    }

    auto const hash = header.hash();

    // Validate against checkpoints
    if (is_checkpoint_conflict(hash, height)) {
        return error::checkpoints_failed;
    }

    return error::success;
}

// Combined validation
// -----------------------------------------------------------------------------

code validate_header::validate(domain::chain::header const& header, size_t height,
                               hash_digest const& previous) const {
    // If under checkpoint, we can skip PoW validation (trusted headers)
    // This is a significant optimization during IBD
    if (!is_under_checkpoint(height)) {
        auto const ec = check(header);
        if (ec) {
            return ec;
        }
    }

    return accept(header, height, previous);
}

// Checkpoint helpers
// -----------------------------------------------------------------------------

bool validate_header::is_checkpoint_conflict(hash_digest const& hash, size_t height) const {
    // Check if there's a checkpoint at this height
    auto const it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
        [height](checkpoint const& cp) {
            return cp.height() == height;
        });

    // If checkpoint exists at this height, hash must match
    if (it != checkpoints_.end()) {
        if (hash != it->hash()) {
            spdlog::warn("[validate_header] Checkpoint conflict at height {}: "
                "received hash {} but expected {}",
                height, encode_hash(hash), encode_hash(it->hash()));
            return true;
        }
    }

    return false;
}

bool validate_header::is_under_checkpoint(size_t height) const {
    return height <= last_checkpoint_height();
}

bool validate_header::is_checkpoint_height(size_t height) const {
    return std::any_of(checkpoints_.begin(), checkpoints_.end(),
        [height](checkpoint const& cp) {
            return cp.height() == height;
        });
}

std::optional<hash_digest> validate_header::checkpoint_hash(size_t height) const {
    auto const it = std::find_if(checkpoints_.begin(), checkpoints_.end(),
        [height](checkpoint const& cp) {
            return cp.height() == height;
        });

    if (it != checkpoints_.end()) {
        return it->hash();
    }

    return std::nullopt;
}

size_t validate_header::last_checkpoint_height() const {
    if (checkpoints_.empty()) {
        return 0;
    }

    // Checkpoints are sorted, so last one has highest height
    return checkpoints_.back().height();
}

} // namespace kth::blockchain
