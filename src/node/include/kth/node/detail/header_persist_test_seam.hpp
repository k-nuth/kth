// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/node/detail/` is excluded from the install rules, so nothing here is part
// of the surface a consumer can reach.

#ifndef KTH_NODE_DETAIL_HEADER_PERSIST_TEST_SEAM_HPP_
#define KTH_NODE_DETAIL_HEADER_PERSIST_TEST_SEAM_HPP_

#include <cstdint>

namespace kth::node::sync::detail {

/// Make the durable header barrier refuse, from a chosen height upwards.
///
/// The barrier's whole purpose is what it prevents: a batch that exists, whose
/// headers cannot be made durable, must apply no delta, move no marker and
/// publish no chain state. That cannot be staged from outside. Taking the
/// heights off the active chain also stops the bodies being stored, so no batch
/// is ever formed and there is nothing for the barrier to refuse -- the failure
/// has to arrive AFTER the batch exists, at the moment the barrier asks.
///
/// Inert unless armed: the check is one comparison against a value no production
/// path writes.
void fail_header_persistence_at_or_above(uint32_t height);

/// Restore ordinary behaviour.
void clear_header_persistence_fault();



} // namespace kth::node::sync::detail

#endif // KTH_NODE_DETAIL_HEADER_PERSIST_TEST_SEAM_HPP_
