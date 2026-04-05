// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Forwarding header — header_index lives in kth::database.
// Re-exported into kth::blockchain for backward compatibility.

#ifndef KTH_BLOCKCHAIN_HEADER_INDEX_HPP
#define KTH_BLOCKCHAIN_HEADER_INDEX_HPP

#include <kth/database/header_index.hpp>

namespace kth::blockchain {

using kth::database::header_status;
using kth::database::header_entry;
using kth::database::header_index_result;
using kth::database::hash_digest_hasher;
using kth::database::header_index;
using kth::database::has_flag;

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_HEADER_INDEX_HPP
