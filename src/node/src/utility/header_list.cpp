// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/utility/header_list.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <kth/node/define.hpp>
#include <kth/node/utility/check_list.hpp>

namespace kth::node {

using namespace kth::domain::chain;
using namespace kth::domain::config;

// Locking is optimized for a single intended caller.
header_list::header_list(size_t slot, infrastructure::config::checkpoint const& start, infrastructure::config::checkpoint const& stop)
    : height_(*safe_add(start.height(), size_t(1)))
    , start_(start)
    , stop_(stop)
    , slot_(slot)
{
    list_.reserve(*safe_subtract(stop.height(), start.height()));
}

bool header_list::complete() const {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    shared_lock lock(mutex_);

    return remaining() == 0;
    ///////////////////////////////////////////////////////////////////////////
}

size_t header_list::slot() const {
    return slot_;
}

size_t header_list::first_height() const {
    return height_;
}

hash_digest header_list::previous_hash() const {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    shared_lock lock(mutex_);

    return list_.empty() ? start_.hash() : list_.back().hash();
    ///////////////////////////////////////////////////////////////////////////
}

size_t header_list::previous_height() const {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    shared_lock lock(mutex_);

    // This addition is safe.
    return start_.height() + list_.size();
    ///////////////////////////////////////////////////////////////////////////
}

hash_digest const& header_list::stop_hash() const {
    return stop_.hash();
}

// This is not thread safe, call only after complete.
const domain::chain::header::list& header_list::headers() const {
    return list_;
}

bool header_list::merge(headers_const_ptr message) {
    auto const& headers = message->elements();

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    unique_lock lock(mutex_);

    auto const count = std::min(remaining(), headers.size());
    auto const end = headers.begin() + count;

    for (auto it = headers.begin(); it != end; ++it) {
        auto const& header = *it;

        if ( ! link(header) || ! check(header) || ! accept(header)) {
            list_.clear();
            return false;
        }

        list_.push_back(header);
    }

    return true;
    ///////////////////////////////////////////////////////////////////////////
}

////checkpoint::list header_list::to_checkpoints() const
////{
////    ///////////////////////////////////////////////////////////////////////////
////    // Critical Section.
////    shared_lock lock(mutex_);
////
////    if ( ! complete() || list_.empty())
////        return{};
////
////    checkpoint::list out;
////    out.reserve(list_.size());
////    auto height = start_.height();
////
////    // The height is safe from overflow.
////    for (auto const& header: list_)
////        out.emplace_back(header.hash(), height++);
////
////    return out;
////    ///////////////////////////////////////////////////////////////////////////
////}

// private
//-----------------------------------------------------------------------------

size_t header_list::remaining() const {
    // This difference is safe from underflow.
    return list_.capacity() - list_.size();
}

bool header_list::link(const domain::chain::header& header) const {
    if (list_.empty()) {
        return header.previous_block_hash() == start_.hash();
    }

    // Avoid copying and temporary reference assignment.
    return header.previous_block_hash() == list_.back().hash();
}

bool header_list::check(header const& header) const {
    // This is a hack for successful compile - this is dead code.
    static auto const retarget = true;

    // This validates is_valid_proof_of_work and is_valid_time_stamp.
    return ! header.check(retarget);
}

bool header_list::accept(header const& header) const {
    //// Parallel header download precludes validation of minimum_version,
    //// work_required and median_time_past, however checkpoints are verified.
    ////return !header.accept(...);

    // Verify last checkpoint.
    return remaining() > 1 || header.hash() == stop_.hash();
}

} // namespace kth::node
