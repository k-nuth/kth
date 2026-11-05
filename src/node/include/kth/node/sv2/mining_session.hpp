// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_MINING_SESSION_HPP
#define KTH_NODE_SV2_MINING_SESSION_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <kth/blockchain/pools/block_template.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/mining/work.hpp>
#include <kth/node/sv2/framing.hpp>

// Mining Protocol state for one connection (sv2-spec 05). The socket loop
// turns `result` into bytes; channel, job and share rules live in work_engine
// so a later Stratum V1 front end can reuse them without these frames.

namespace kth::node::sv2 {

struct mining_session {
    struct result {
        data_chunk response;
        bool close = false;
        bool wants_template = false;
        block_const_ptr solution;
        // Set when the share's coinbase is the one declared to the pool.
        std::optional<mining::pool_proof> pool;
    };

    mining_session(uint64_t connection_salt, domain::chain::script payout,
        std::shared_ptr<uint64_t> pool_allocations = {});

    [[nodiscard]]
    result process(message_header const& header, byte_span payload);

    // Future job, then the Mining Protocol SetNewPrevHash, for every channel.
    [[nodiscard]]
    result apply(blockchain::mining_template value);

    // With JD's downstream reserve every channel fits the declared extranonce.
    // The template selection is the same one solo work uses.
    [[nodiscard]]
    result apply_pool(mining::declared_work const& work);

    // Drop work as soon as the published tip is no longer the one being mined.
    void note_tip(hash_digest const& tip);

    [[nodiscard]]
    bool ready() const;

    // A channel is open and its job was staled or never built.
    [[nodiscard]]
    bool needs_template() const;

private:
    enum class state { awaiting_setup, ready, closed };

    struct pending_channel {
        uint32_t id;
        uint32_t group_id;
        data_chunk response;
    };

    [[nodiscard]] result close();
    [[nodiscard]] result finish_open(mining::open_request const& request);
    [[nodiscard]] result finish_share(uint8_t message_type, byte_span payload);
    [[nodiscard]] result emit(std::vector<mining::work_job> const& jobs,
        blockchain::mining_template value);
    [[nodiscard]] domain::chain::output::list coinbase_outputs(uint64_t value) const;
    [[nodiscard]] data_chunk job_frames(mining::work_job const& job) const;

    mining::work_engine engine_;
    domain::chain::script payout_;
    state state_ = state::awaiting_setup;
    bool standard_only_ = false;
    std::vector<pending_channel> pending_;
    std::optional<blockchain::mining_template> current_;
    std::optional<hash_digest> template_prev_;
};

} // namespace kth::node::sv2

#endif
