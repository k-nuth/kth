// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_LOCAL_BLOCK_ADMISSION_HPP
#define KTH_BLOCKCHAIN_LOCAL_BLOCK_ADMISSION_HPP

#include <memory>
#include <system_error>
#include <asio/experimental/channel_select.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using block_submission_reply = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;

// The admission owner answers once, after rejection or connected publication.
// An abandoned waiter does not cancel consensus work already admitted.
struct block_submission {
    block_const_ptr block;
    std::shared_ptr<block_submission_reply> reply;
};

using block_submission_channel = ::asio::experimental::select_channel<block_submission>;

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_LOCAL_BLOCK_ADMISSION_HPP
