// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_MINING_WORK_HPP
#define KTH_NODE_MINING_WORK_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <kth/blockchain/pools/block_template.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/config/network.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/settings.hpp>

// Channel, job and share engine shared by Stratum front ends. It does not know
// about SV2 frames: a front end feeds it templates, coinbase outputs and share
// fields, and turns the results into its own messages. The coinbase output list
// is an input so a later payout split can replace the single configured output
// without another job builder.

namespace kth::node::mining {

// One share per this many seconds at the channel's nominal hashrate, clamped
// to the device's maximum target. An unknown hashrate (+0) uses that maximum.
inline constexpr uint32_t share_interval_seconds = 20;

// BCH rejects a block timestamp more than two hours ahead of the node. The
// protocol leaves the upper nTime bound to the server; this is that local bound.
inline constexpr uint32_t ntime_roll_seconds = 2 * 60 * 60;

// BIP323 names 24 general-purpose version bits. BCH templates use 0x20000000
// as the version base (bit 29), so the rolling mask is bits 5..28 and leaves
// that base bit alone. There is no witness commitment to protect.
inline constexpr uint32_t version_rolling_mask = 0x1fffffe0u;

inline constexpr size_t max_mining_channels = 32;
inline constexpr size_t max_jobs_per_channel = 8;
inline constexpr size_t max_remembered_shares = 1024;
inline constexpr size_t max_remembered_requests = 1024;
inline constexpr size_t extranonce_prefix_size = 12;
inline constexpr size_t coinbase_script_limit = 100;

// Share and channel strings this server documents. sv2-spec 3.5 leaves the
// text to the implementation and requires it to be printable ASCII.
inline constexpr std::string_view error_invalid_channel_id = "invalid-channel-id";
inline constexpr std::string_view error_invalid_job_id = "invalid-job-id";
inline constexpr std::string_view error_stale_share = "stale-share";
inline constexpr std::string_view error_invalid_ntime = "invalid-ntime";
inline constexpr std::string_view error_invalid_version = "invalid-version";
inline constexpr std::string_view error_difficulty_too_low = "difficulty-too-low";
inline constexpr std::string_view error_duplicate_share = "duplicate-share";
inline constexpr std::string_view error_share_limit = "share-limit";
inline constexpr std::string_view error_invalid_extranonce_size = "invalid-extranonce-size";
inline constexpr std::string_view error_invalid_channel_type = "invalid-channel-type";
inline constexpr std::string_view error_duplicate_request_id = "duplicate-request-id";
inline constexpr std::string_view error_too_many_channels = "too-many-channels";
inline constexpr std::string_view error_extranonce_too_large = "min-extranonce-size-too-large";
inline constexpr std::string_view error_requires_standard_jobs = "requires-standard-jobs";

struct payout {
    domain::chain::script script;
};

// Empty when mining is off. Otherwise a reason the node must not start it: the
// listener is off, the address is missing or not a cashaddr for `network`, or
// the deadline for a first channel is zero.
[[nodiscard]]
std::string mining_startup_reason(sv2_settings const& settings, domain::config::network network);

// The script the configured cashaddr pays. `error` explains a refusal.
[[nodiscard]]
std::optional<payout> payout_from_address(std::string_view address,
    domain::config::network network, std::string& error);

enum class channel_kind { standard, extended };

struct open_request {
    uint32_t request_id = 0;
    float nominal_hash_rate = 0;
    hash_digest max_target{};
    uint16_t min_extranonce_size = 0;
    channel_kind kind = channel_kind::standard;
    // The caller asked only for header-only channels (SV2 REQUIRES_STANDARD_JOBS).
    bool standard_only = false;
};

enum class open_status { opened, rejected, protocol_error };

struct opened_channel {
    open_status status = open_status::rejected;
    std::string error_code;
    uint32_t channel_id = 0;
    uint32_t group_channel_id = 0;
    hash_digest target{};
    data_chunk extranonce_prefix;
    uint16_t extranonce_size = 0;
};

// One future job plus the prevhash that activates it. The front end sends the
// job with nTime unset, then SetNewPrevHash. Shares on it are therefore held
// to the channel's latest target (sv2-spec 5.3.21). `merkle_root` is meaningful
// for a standard channel; an extended channel uses `merkle_path` instead.
struct work_job {
    bool extended = false;
    uint32_t channel_id = 0;
    uint32_t job_id = 0;
    uint32_t version = 0;
    bool version_rolling_allowed = true;
    hash_digest merkle_root{};
    std::vector<hash_digest> merkle_path;
    data_chunk coinbase_tx_prefix;
    data_chunk coinbase_tx_suffix;
    hash_digest prev_hash{};
    uint32_t ntime_start = 0;
    uint32_t nbits = 0;
};

struct update_result {
    bool protocol_error = false;
    bool unknown = false;
    std::optional<hash_digest> target;
    uint32_t channel_id = 0;
};

struct share_request {
    bool extended = false;
    uint32_t channel_id = 0;
    uint32_t sequence_number = 0;
    uint32_t job_id = 0;
    uint32_t nonce = 0;
    uint32_t ntime = 0;
    uint32_t version = 0;
    data_chunk extranonce;
};

enum class share_status { accepted, block, rejected };

struct share_outcome {
    share_status status = share_status::rejected;
    std::string error_code;
    uint32_t channel_id = 0;
    uint32_t sequence_number = 0;
    uint32_t accepted_count = 0;
    uint64_t shares_sum = 0;
    block_const_ptr block;
    // Issue fresh work before the bounded duplicate history fills up.
    bool renew = false;
};

class work_engine {
public:
    // `connection_salt` keeps extranonce prefixes from colliding across
    // connections that would otherwise reuse channel ids.
    explicit work_engine(uint64_t connection_salt);

    [[nodiscard]] opened_channel open(open_request const& request);
    [[nodiscard]] update_result update(uint32_t channel_id, float nominal_hash_rate,
        hash_digest const& max_target);
    // False when nothing was open under that channel or group id.
    bool close(uint32_t channel_id);
    void stale_all();

    // Rebuild every open channel onto this template. Outputs are copied into
    // the coinbase; their values must sum to the template's coinbase value.
    [[nodiscard]] std::vector<work_job> on_template(blockchain::mining_template const& tmpl,
        domain::chain::output::list const& outputs);

    // The same for one channel that opened onto a template already in force.
    [[nodiscard]] std::optional<work_job> on_channel(uint32_t channel_id,
        blockchain::mining_template const& tmpl, domain::chain::output::list const& outputs);

    [[nodiscard]] share_outcome submit(share_request const& share);

    [[nodiscard]] size_t channel_count() const;
    [[nodiscard]] bool has_active_job() const;

private:
    struct job {
        uint32_t id = 0;
        bool active = false;
        bool stale = false;
        bool extended = false;
        bool version_rolling = true;
        uint32_t version = 0;
        hash_digest prev_hash{};
        uint32_t ntime_start = 0;
        uint32_t bits = 0;
        hash_digest merkle_root{};
        std::vector<hash_digest> merkle_path;
        data_chunk coinbase_tx_prefix;
        data_chunk coinbase_tx_suffix;
        data_chunk extranonce_prefix;
        uint16_t extranonce_size = 0;
        std::shared_ptr<std::vector<transaction_const_ptr> const> txs;
    };

    struct share_key {
        uint32_t job_id = 0;
        uint32_t nonce = 0;
        uint32_t ntime = 0;
        uint32_t version = 0;
        data_chunk extranonce;
        bool operator==(share_key const&) const = default;
    };

    struct channel {
        uint32_t id = 0;
        uint32_t group_id = 0;
        channel_kind kind = channel_kind::standard;
        float hashrate = 0;
        hash_digest max_target{};
        hash_digest share_target{};
        data_chunk extranonce_prefix;
        uint16_t extranonce_size = 0;
        std::vector<job> jobs;
        std::vector<share_key> shares;
    };

    [[nodiscard]] hash_digest choose_target(float hashrate, hash_digest const& max_target) const;
    [[nodiscard]] std::optional<work_job> build(channel& slot, blockchain::mining_template const& tmpl,
        domain::chain::output::list const& outputs);
    static void invalidate(job& item);
    [[nodiscard]] channel* find(uint32_t channel_id);

    uint64_t const salt_;
    uint32_t next_channel_ = 1;
    uint32_t next_job_ = 1;
    std::vector<uint32_t> requests_;
    std::vector<channel> channels_;
    std::shared_ptr<std::vector<transaction_const_ptr> const> current_txs_;
};

} // namespace kth::node::mining

#endif
