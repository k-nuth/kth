// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_LOCAL_BLOCK_ADMISSION_HPP
#define KTH_BLOCKCHAIN_LOCAL_BLOCK_ADMISSION_HPP

#include <memory>
#include <system_error>
#include <asio/experimental/channel_select.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

// Validity and duplication are independent. Stored but unconnected data and
// operational failures have unknown validity, never an implied consensus verdict.
enum class block_validity : uint8_t {
    unknown = 0,
    valid = 1,
    invalid = 2,
};

// A single submit_block operation returns the owner's complete reply.
struct block_submission_result {
    code error;
    block_validity validity{block_validity::unknown};
    bool duplicate{false};

    block_submission_result(code ec = {})
        : error(ec)
        , validity(ec ? block_validity::unknown : block_validity::valid) {}

    block_submission_result(kth::error::error_code_t ec)
        : block_submission_result(code(ec)) {}

    block_submission_result(code ec, block_validity state, bool repeated = false)
        : error(ec), validity(state), duplicate(repeated) {}
};

// Executor-free, like the submission endpoint. The owner may hold the reply
// after the submitter is gone, together with its executor; a channel bound to
// that executor would reach into the destroyed context when the owner drops it.
using block_submission_reply = ::asio::experimental::select_channel<block_submission_result>;

// The admission owner answers once, after rejection or connected publication.
// An abandoned waiter does not cancel consensus work already admitted.
struct block_submission {
    block_const_ptr block;
    std::shared_ptr<block_submission_reply> reply;
};

using block_submission_channel = ::asio::experimental::select_channel<block_submission>;

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_LOCAL_BLOCK_ADMISSION_HPP
