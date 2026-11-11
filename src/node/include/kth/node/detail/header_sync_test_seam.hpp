// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.
//
// `kth/node/detail/` is excluded from the install rules, so nothing here is part
// of the surface a consumer can reach.

#ifndef KTH_NODE_DETAIL_HEADER_SYNC_TEST_SEAM_HPP_
#define KTH_NODE_DETAIL_HEADER_SYNC_TEST_SEAM_HPP_

#if defined(KTH_BROWSER_NODE)
namespace kth::node::detail {

/// Make the next transaction-request task of a header_sync session throw, as
/// a library failure (allocation, Asio) would, while its session goes on.
///
/// The session must learn of it and end: nothing else stops it while its peer
/// keeps answering. That cannot be staged from outside, since no input makes
/// the task fail. Inert unless armed; it disarms itself when it fires.
void fail_next_transaction_requests();

} // namespace kth::node::detail
#endif

#endif // KTH_NODE_DETAIL_HEADER_SYNC_TEST_SEAM_HPP_
