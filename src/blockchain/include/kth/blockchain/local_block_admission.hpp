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

// What a block proposal (BIP23) comes to: the answer of
// block_chain::test_block_proposal. Nothing about a proposal is stored, so the
// same block proposed again is judged again.
struct block_proposal_result {
    enum class outcome : uint8_t {
        // Valid as the next block on the connected tip, proof of work aside.
        valid,
        // The node holds the block already. `validity` is what it knows of it:
        // valid once connected, invalid once refused, unknown otherwise.
        duplicate,
        // Built on another block than the connected tip, so not judged. Also
        // the answer when the tip kept moving while the block was judged.
        not_on_tip,
        // A consensus rule refuses it. `error` is the first one it breaks;
        // `script` says the script checks refused it (an input script failed
        // or a SigChecks limit was exceeded), whatever the code.
        invalid,
        // No verdict. `error` says why: the node is stopping, or a store did
        // not answer.
        failed,
    };
    outcome result{outcome::failed};
    code error{};
    block_validity validity{block_validity::unknown};
    bool script{false};
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
