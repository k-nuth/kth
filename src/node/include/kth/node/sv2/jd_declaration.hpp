// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_JD_DECLARATION_HPP
#define KTH_NODE_SV2_JD_DECLARATION_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include <kth/node/mining/work.hpp>
#include <kth/node/sv2/jd_messages.hpp>
#include <kth/node/sv2/jd_session.hpp>

namespace kth::node::sv2 {

// One outstanding local selection. The owner supplies deadlines and queues
// complete frames; neither a pool reply nor missing transaction data can wait
// on a downstream miner or the block connection path.
struct jd_declaration {
    struct result {
        data_chunk mining;
        data_chunk declaration;
        bool progress = false;
        bool accepted = false;
        std::string error;
    };

    jd_declaration(std::string identity, bool full_template);

    [[nodiscard]] result start(blockchain::mining_template value,
        open_extended_mining_channel_success const& channel);
    [[nodiscard]] result process(jd_peer peer, message_header const& header, byte_span payload);
    [[nodiscard]] bool pending() const;
    [[nodiscard]] std::optional<mining::declared_work> const& work() const;
    // The template last given to start(), kept across a refusal so fallback
    // can pay the solo address from the same selection.
    [[nodiscard]] bool has_template() const;
    [[nodiscard]] blockchain::mining_template const& template_value() const;
    // Full-template only, and only after DeclareMiningJob.Success. Spec 6.4.9
    // does not define PushSolution for coinbase-only.
    [[nodiscard]] bool can_push_solution() const;
    // Coinbase-only never sends PushSolution. Callers use this so they do not
    // hold a block they are not going to push.
    [[nodiscard]] bool full_template() const;
    // Drop an in-flight declaration whose tip is no longer current. Its
    // request id stays recognized, so a late reply is ignored after the next
    // declaration has started.
    void supersede();
    void note_target(hash_digest const& target);

private:
    enum class state { idle, allocating, declaring, installing, accepted, failed };

    [[nodiscard]] result fail(std::string reason);
    [[nodiscard]] result install(data_chunk token);
    [[nodiscard]] bool retired(uint32_t id) const;

    // One id per replaced declaration. A handful of tip changes can outrun a
    // reply; an id older than this is treated as one this client never sent.
    static constexpr size_t max_retired_request_ids = 8;

    std::string identity_;
    bool full_template_;
    bool has_template_ = false;
    state state_ = state::idle;
    uint32_t request_id_ = 0;
    std::deque<uint32_t> retired_request_ids_;
    blockchain::mining_template template_{};
    open_extended_mining_channel_success channel_;
    std::optional<mining::declared_work> work_;
    std::vector<data_chunk> transactions_;
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_JD_DECLARATION_HPP
