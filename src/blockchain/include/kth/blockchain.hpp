// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_HPP
#define KTH_BLOCKCHAIN_HPP

/**
 * API Users: Include only this header. Direct use of other headers is fragile
 * and unsupported as header organization is subject to change.
 *
 * Maintainers: Do not include this header internal to this library.
 */

#include <kth/database.hpp>

#ifdef WITH_CONSENSUS
#include <kth/consensus.hpp>
#endif

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/block_entry.hpp>
#include <kth/blockchain/pools/block_organizer.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/blockchain/pools/transaction_entry.hpp>
#include <kth/blockchain/pools/transaction_organizer.hpp>
#include <kth/blockchain/pools/transaction_pool.hpp>
#include <kth/blockchain/populate/populate_base.hpp>
#include <kth/blockchain/populate/populate_block.hpp>
#include <kth/blockchain/populate/populate_chain_state.hpp>
#include <kth/blockchain/populate/populate_transaction.hpp>
#include <kth/blockchain/validate/validate_block.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/blockchain/validate/validate_input.hpp>
#include <kth/blockchain/validate/validate_transaction.hpp>

#endif
