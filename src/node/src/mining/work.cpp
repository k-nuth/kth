// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/mining/work.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/node/mining/block_submit.hpp>

namespace kth::node::mining {

namespace {

// Bitcoin's difficulty-1 compact target. Share difficulty is this value
// divided by the share target (sv2-spec 5.3.13).
constexpr uint32_t diff1_bits = 0x1d00ffffu;

// BIP34 pushes are at most 8 bytes in the templates this node already serves.
constexpr size_t bip34_reserve = 8;

constexpr size_t max_coinbase_outputs = 256;
constexpr size_t max_output_bytes = 64 * 1024;
constexpr size_t b0_64k_limit = 65535;

// Group ids share the channel-id namespace and must not collide with one
// (sv2-spec 5.2.3). The high bit marks a group; channel ids stay below it.
constexpr uint32_t group_bit = 0x80000000u;

bool bad_float(float value) {
    auto const bits = std::bit_cast<uint32_t>(value);
    auto const exponent = (bits >> 23) & 0xffu;
    bool const negative = (bits & 0x80000000u) != 0;
    return negative || exponent == 0xffu;
}

hash_digest bytes_from(uint256_t value) {
    hash_digest out{};
    for (auto& byte : out) {
        byte = static_cast<uint8_t>(value & 0xffu);
        value >>= 8;
    }
    return out;
}

uint64_t share_difficulty(hash_digest const& target) {
    auto const value = to_uint256(target);
    if (value == 0) return 0;
    auto const diff1 = domain::chain::compact::from_compact(diff1_bits);
    if ( ! diff1 || value > diff1->big()) return 0;
    auto const quotient = diff1->big() / value;
    auto const limit = std::numeric_limits<uint64_t>::max();
    if (quotient > limit) return limit;
    return static_cast<uint64_t>(quotient);
}

hash_digest merkle_parent(hash_digest const& left, hash_digest const& right) {
    return bitcoin_hash(build_chunk({left, right}));
}

// Same branch a Template Distribution client reconstructs: the coinbase is
// index 0, so a placeholder hash does not change any sibling on its path.
std::vector<hash_digest> merkle_path_of(std::vector<transaction_const_ptr> const& txs) {
    std::vector<hash_digest> hashes{null_hash};
    hashes.reserve(txs.size() + 1);
    for (auto const& tx : txs) hashes.push_back(tx->hash());
    std::vector<hash_digest> path;
    while (hashes.size() > 1) {
        if (hashes.size() % 2 != 0) hashes.push_back(hashes.back());
        path.push_back(hashes[1]);
        auto const pairs = hashes.size() / 2;
        for (size_t i = 0; i < pairs; ++i) {
            hashes[i] = merkle_parent(hashes[2 * i], hashes[2 * i + 1]);
        }
        hashes.resize(pairs);
    }
    return path;
}

hash_digest fold_merkle(hash_digest root, std::vector<hash_digest> const& path) {
    for (auto const& sibling : path) root = merkle_parent(root, sibling);
    return root;
}

bool starts_with_prefix(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size() + 1 || text[prefix.size()] != ':') return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        auto const left = std::tolower(static_cast<unsigned char>(text[i]));
        auto const right = std::tolower(static_cast<unsigned char>(prefix[i]));
        if (left != right) return false;
    }
    return true;
}

struct coinbase_parts {
    data_chunk prefix;
    data_chunk suffix;
    hash_digest txid{};
    size_t size = 0;
};

// BCH coinbase: version, one null input, BIP34, the channel extranonce, the
// supplied outputs, locktime 0. There is no segwit marker and no witness
// commitment to strip (sv2-spec 5.3.16 assumes a Bitcoin coinbase).
std::optional<coinbase_parts> split_coinbase(size_t height, uint32_t job_id,
    data_chunk const& extranonce_prefix, uint16_t extranonce_size,
    domain::chain::output::list const& outputs) {
    if (outputs.empty() || outputs.size() > max_coinbase_outputs) return std::nullopt;
    uint64_t output_bytes = 0;
    for (auto const& output : outputs) {
        output_bytes += output.serialized_size(true);
        if (output_bytes > max_output_bytes) return std::nullopt;
    }

    if (height > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return std::nullopt;
    auto const number = infrastructure::machine::number::from_int(static_cast<int64_t>(height));
    if ( ! number) return std::nullopt;
    auto bip34 = domain::machine::operation(number->data()).to_data();
    if (bip34.empty() || bip34.size() > bip34_reserve) return std::nullopt;
    // A replacement on the same tip must give the miner new hash space even
    // when its payout and channel extranonce have not changed.
    auto const generation = to_little_endian(job_id);
    bip34.insert(bip34.end(), generation.begin(), generation.end());
    // BCH required 100-byte transactions before CashTokens reduced the
    // minimum to 65. Keep these coinbases valid on both sides of activation.
    auto const fixed_size = 4 + 1 + 36 + 1 + 4 + (outputs.size() < 253 ? 1 : 3)
        + output_bytes + 4;
    auto const unpadded = fixed_size + bip34.size() + extranonce_prefix.size() + extranonce_size;
    if (unpadded < 100) bip34.insert(bip34.end(), 100 - unpadded, uint8_t{0});
    auto const body = bip34.size() + extranonce_prefix.size() + size_t{extranonce_size};
    // A one-byte script length keeps the extranonce at a fixed offset. The
    // consensus scriptSig limit is 100 bytes and is already below that.
    if (body > coinbase_script_limit || body >= 253) return std::nullopt;

    data_chunk script_bytes;
    script_bytes.reserve(body);
    script_bytes.insert(script_bytes.end(), bip34.begin(), bip34.end());
    script_bytes.insert(script_bytes.end(), extranonce_prefix.begin(), extranonce_prefix.end());
    script_bytes.insert(script_bytes.end(), size_t{extranonce_size}, uint8_t{0});
    // prefix=false keeps these bytes as the script body. Re-parsing them would
    // turn the placeholder zeros into pushes and move the extranonce split.
    domain::chain::script script(std::move(script_bytes), false);
    domain::chain::input input(domain::chain::output_point::null(), std::move(script), 0xffffffffu);
    domain::chain::transaction transaction(1, 0, {std::move(input)}, outputs);
    auto const raw = to_data_chunk(transaction, true);

    constexpr size_t length_at = 4 + 1 + 36;
    if (raw.size() < length_at + 1 + body || raw[4] != 1 || raw[length_at] != body) {
        return std::nullopt;
    }
    auto const middle = extranonce_prefix.size() + extranonce_size;
    auto const head = length_at + 1 + bip34.size();
    if (raw.size() < head + middle) return std::nullopt;
    if ( ! std::equal(extranonce_prefix.begin(), extranonce_prefix.end(),
            raw.begin() + static_cast<std::ptrdiff_t>(head))) {
        return std::nullopt;
    }
    if ( ! std::all_of(raw.begin() + static_cast<std::ptrdiff_t>(head + extranonce_prefix.size()),
            raw.begin() + static_cast<std::ptrdiff_t>(head + middle),
            [](uint8_t byte) { return byte == 0; })) {
        return std::nullopt;
    }
    auto const tail = head + middle;
    if (head > b0_64k_limit || raw.size() - tail > b0_64k_limit) return std::nullopt;

    coinbase_parts parts;
    parts.prefix.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(head));
    parts.suffix.assign(raw.begin() + static_cast<std::ptrdiff_t>(tail), raw.end());
    parts.txid = transaction.hash();
    parts.size = raw.size();
    return parts;
}

bool outputs_pay(domain::chain::output::list const& outputs, uint64_t coinbase_value) {
    uint64_t sum = 0;
    for (auto const& output : outputs) {
        // BCH permits CashTokens in selected transactions, but a coinbase
        // cannot create tokens. A payout split must obey that rule too.
        if (output.token_data()) return false;
        if (sum > std::numeric_limits<uint64_t>::max() - output.value()) return false;
        sum += output.value();
    }
    return sum == coinbase_value;
}

opened_channel rejected(std::string_view code) {
    opened_channel result;
    result.status = open_status::rejected;
    result.error_code = std::string(code);
    return result;
}

share_outcome rejected_share(share_request const& share, std::string_view code) {
    share_outcome result;
    result.status = share_status::rejected;
    result.error_code = std::string(code);
    result.channel_id = share.channel_id;
    result.sequence_number = share.sequence_number;
    return result;
}

} // namespace

std::string mining_startup_reason(sv2_settings const& settings, domain::config::network network) {
    if ( ! settings.mining.enabled) return {};
    if ( ! settings.enabled) return "sv2.mining.enabled requires sv2.enabled";
    if (settings.mining.address.empty()) return "sv2.mining.enabled requires sv2.mining.address";
    std::string error;
    if ( ! payout_from_address(settings.mining.address, network, error)) return error;
    return {};
}

std::optional<payout> payout_from_address(std::string_view address,
    domain::config::network network, std::string& error) {
    error.clear();
#if ! defined(KTH_CURRENCY_BCH)
    (void)address;
    (void)network;
    error = "sv2.mining.address is not a cashaddr for this network";
    return std::nullopt;
#else
    auto const prefix = domain::wallet::payment_address::cashaddr_prefix_for(network);
    if ( ! starts_with_prefix(address, prefix) || address.size() > prefix.size() + 1 + 53 + 8) {
        error = "sv2.mining.address is not a cashaddr for this network";
        return std::nullopt;
    }
    auto const [decoded_prefix, payload] = domain::wallet::cashaddr::decode(
        std::string{address}, std::string{prefix});
    if (decoded_prefix != prefix || (payload.size() != 34 && payload.size() != 53)) {
        error = "sv2.mining.address is not a cashaddr for this network";
        return std::nullopt;
    }
    data_chunk decoded;
    uint32_t accumulator = 0;
    unsigned bits = 0;
    for (auto const value : payload) {
        accumulator = (accumulator << 5) | value;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            decoded.push_back(static_cast<uint8_t>(accumulator >> bits));
        }
    }
    auto const version = decoded.front();
    auto const type = version >> 3;
    auto const size = decoded.size() - 1;
    bool const script_hash = (type & 1) != 0;
    // CashTokens-aware address types pay the same locking script. Only P2SH
    // has a 32-byte hash form; silently treating P2PKH32 as P2SH would mispay.
    if ((bits != 0 && (accumulator & ((1u << bits) - 1)) != 0)
        || type > 3 || (size == 20 ? (version & 7) != 0 : (version & 7) != 3 || ! script_hash)) {
        error = "sv2.mining.address is not a supported cashaddr";
        return std::nullopt;
    }
    if (size == hash_size) {
        hash_digest hash{};
        std::copy(decoded.begin() + 1, decoded.end(), hash.begin());
        return payout{domain::chain::script(domain::chain::script::to_pay_script_hash_32_pattern(hash))};
    }
    short_hash hash{};
    std::copy(decoded.begin() + 1, decoded.end(), hash.begin());
    return payout{domain::chain::script(script_hash
        ? domain::chain::script::to_pay_script_hash_pattern(hash)
        : domain::chain::script::to_pay_public_key_hash_pattern(hash))};
#endif
}

work_engine::work_engine(uint64_t connection_salt)
    : salt_(connection_salt)
{}

hash_digest work_engine::choose_target(float hashrate, hash_digest const& max_target) const {
    // A hash is uniform over 256 bits. Aim for one share per interval, without
    // rounding small hash rates down to difficulty one. The F32 significand
    // keeps this calculation integral even for very large finite hash rates.
    auto const ceiling = to_uint256(max_target);
    if (hashrate == 0.f) return max_target;
    auto const encoded = std::bit_cast<uint32_t>(hashrate);
    auto const exponent = static_cast<int>((encoded >> 23) & 0xffu);
    uint64_t const significand = ((encoded & 0x7fffffu)
        | (exponent == 0 ? 0u : 0x800000u)) * uint64_t{share_interval_seconds};
    auto const power = exponent == 0 ? -149 : exponent - 150;
    auto const maximum = std::numeric_limits<uint256_t>::max();
    uint256_t chosen = maximum / significand;
    if (power >= 0) {
        chosen >>= power;
    } else {
        auto const shift = -power;
        if (shift >= 256 || chosen > (maximum >> shift)) return max_target;
        // Preserve the remainder when multiplying the numerator by 2^shift.
        // Dividing first without it would unnecessarily harden the target.
        chosen = (chosen << shift) + (((maximum % significand) << shift) / significand);
    }
    if (chosen > ceiling) chosen = ceiling;
    return bytes_from(chosen);
}

opened_channel work_engine::open(open_request const& request) {
    if (bad_float(request.nominal_hash_rate)) {
        opened_channel result;
        result.status = open_status::protocol_error;
        return result;
    }
    if (std::find(requests_.begin(), requests_.end(), request.request_id) != requests_.end()) {
        return rejected(error_duplicate_request_id);
    }
    // A client that never repeats an id still cannot grow this table without
    // bound. Past the cap the connection is no longer following the protocol.
    if (requests_.size() >= max_remembered_requests) {
        opened_channel result;
        result.status = open_status::protocol_error;
        return result;
    }
    requests_.push_back(request.request_id);

    if (channels_.size() >= max_mining_channels || next_channel_ == 0
        || (next_channel_ & group_bit) != 0) {
        return rejected(error_too_many_channels);
    }
    if (request.kind == channel_kind::extended && request.standard_only) {
        return rejected(error_requires_standard_jobs);
    }

    uint16_t extranonce_size = 0;
    if (request.kind == channel_kind::extended) {
        auto const size = std::max<size_t>(request.min_extranonce_size, extranonce_prefix_size);
        if (request.min_extranonce_size > 32 || size > 32
            || bip34_reserve + extranonce_prefix_size + size > coinbase_script_limit) {
            return rejected(error_extranonce_too_large);
        }
        extranonce_size = static_cast<uint16_t>(size);
    }

    channel slot;
    slot.id = next_channel_++;
    slot.group_id = group_bit | slot.id;
    slot.kind = request.kind;
    slot.hashrate = request.nominal_hash_rate;
    slot.max_target = request.max_target;
    slot.share_target = choose_target(request.nominal_hash_rate, request.max_target);
    slot.extranonce_size = extranonce_size;
    auto const salt = to_little_endian(salt_);
    auto const id = to_little_endian(slot.id);
    slot.extranonce_prefix.assign(salt.begin(), salt.end());
    slot.extranonce_prefix.insert(slot.extranonce_prefix.end(), id.begin(), id.end());
    channels_.push_back(std::move(slot));
    auto const& opened_slot = channels_.back();

    opened_channel result;
    result.status = open_status::opened;
    result.channel_id = opened_slot.id;
    result.group_channel_id = opened_slot.group_id;
    result.target = opened_slot.share_target;
    result.extranonce_prefix = opened_slot.extranonce_prefix;
    result.extranonce_size = opened_slot.extranonce_size;
    return result;
}

update_result work_engine::update(uint32_t channel_id, float nominal_hash_rate,
    hash_digest const& max_target) {
    update_result result;
    result.channel_id = channel_id;
    if (bad_float(nominal_hash_rate)) {
        result.protocol_error = true;
        return result;
    }
    auto* slot = find(channel_id);
    if (slot == nullptr) {
        result.unknown = true;
        return result;
    }
    auto const previous = slot->share_target;
    slot->hashrate = nominal_hash_rate;
    slot->max_target = max_target;
    auto const next = choose_target(nominal_hash_rate, max_target);
    // A smaller max_target has to be reflected (sv2-spec 5.3.7). The new
    // target applies to later jobs; the active job keeps the target it was
    // activated with (sv2-spec 5.3.21).
    if (next != previous || to_uint256(max_target) < to_uint256(previous)) {
        slot->share_target = next;
        result.target = next;
    }
    return result;
}

bool work_engine::close(uint32_t channel_id) {
    auto const end = std::remove_if(channels_.begin(), channels_.end(),
        [channel_id](channel const& slot) {
            return slot.id == channel_id || slot.group_id == channel_id;
        });
    bool const removed = end != channels_.end();
    channels_.erase(end, channels_.end());
    return removed;
}

void work_engine::stale_all() {
    for (auto& slot : channels_) {
        for (auto& item : slot.jobs) {
            invalidate(item);
        }
        slot.shares.clear();
    }
    current_txs_.reset();
}

void work_engine::invalidate(job& item) {
    auto const id = item.id;
    // Retain only the id needed to distinguish stale from unknown work.
    // A slow miner must not pin whole selections from earlier tips.
    item = job{};
    item.id = id;
    item.stale = true;
}

std::optional<work_job> work_engine::build(channel& slot,
    blockchain::mining_template const& tmpl, domain::chain::output::list const& outputs) {
    for (auto& item : slot.jobs) {
        invalidate(item);
    }
    slot.shares.clear();
    auto const network_target = domain::chain::compact::from_compact(tmpl.bits);
    if ( ! network_target || network_target->big() == 0 || tmpl.current_time < tmpl.min_time) {
        return std::nullopt;
    }
    if ( ! outputs_pay(outputs, tmpl.coinbase_value)) {
        spdlog::warn("[mining] Skipping channel {}: invalid coinbase outputs",
            slot.id);
        return std::nullopt;
    }
    auto parts = split_coinbase(tmpl.height, next_job_, slot.extranonce_prefix,
        slot.extranonce_size, outputs);
    if ( ! parts) {
        spdlog::warn("[mining] Skipping channel {}: coinbase does not fit the Mining Protocol fields",
            slot.id);
        return std::nullopt;
    }
    auto const overhead = domain::chain::header{}.serialized_size(true)
        + infrastructure::message::variable_uint_size(tmpl.selection.txs.size() + 1);
    if (overhead > tmpl.size_limit || parts->size > tmpl.size_limit - overhead
        || tmpl.selection.total_size > tmpl.size_limit - overhead - parts->size) {
        return std::nullopt;
    }
    auto path = merkle_path_of(tmpl.selection.txs);
    if (path.size() > 255) {
        spdlog::warn("[mining] Skipping channel {}: merkle path exceeds SEQ0_255", slot.id);
        return std::nullopt;
    }

    if (slot.jobs.size() >= max_jobs_per_channel) slot.jobs.erase(slot.jobs.begin());

    job created;
    created.id = next_job_++;
    created.active = true;
    created.extended = slot.kind == channel_kind::extended;
    created.version_rolling = true;
    created.version = tmpl.version;
    created.prev_hash = tmpl.previous_block_hash;
    created.ntime_start = tmpl.current_time;
    created.bits = tmpl.bits;
    // Frozen at activation. A later SetTarget does not retarget this job.
    created.share_target = slot.share_target;
    created.merkle_root = fold_merkle(parts->txid, path);
    created.merkle_path = std::move(path);
    created.coinbase_tx_prefix = std::move(parts->prefix);
    created.coinbase_tx_suffix = std::move(parts->suffix);
    created.extranonce_prefix = slot.extranonce_prefix;
    created.extranonce_size = slot.extranonce_size;
    created.txs = current_txs_;
    slot.jobs.push_back(std::move(created));
    auto const& stored = slot.jobs.back();

    work_job out;
    out.extended = stored.extended;
    out.channel_id = slot.id;
    out.job_id = stored.id;
    out.version = stored.version;
    out.version_rolling_allowed = stored.version_rolling;
    out.merkle_root = stored.merkle_root;
    out.merkle_path = stored.merkle_path;
    out.coinbase_tx_prefix = stored.coinbase_tx_prefix;
    out.coinbase_tx_suffix = stored.coinbase_tx_suffix;
    out.prev_hash = stored.prev_hash;
    out.ntime_start = stored.ntime_start;
    out.nbits = stored.bits;
    return out;
}

std::vector<work_job> work_engine::on_template(blockchain::mining_template const& tmpl,
    domain::chain::output::list const& outputs) {
    current_txs_ = std::make_shared<std::vector<transaction_const_ptr> const>(tmpl.selection.txs);
    std::vector<work_job> jobs;
    jobs.reserve(channels_.size());
    for (auto& slot : channels_) {
        if (auto job = build(slot, tmpl, outputs)) jobs.push_back(std::move(*job));
    }
    return jobs;
}

std::optional<work_job> work_engine::on_channel(uint32_t channel_id,
    blockchain::mining_template const& tmpl, domain::chain::output::list const& outputs) {
    auto* slot = find(channel_id);
    if (slot == nullptr) return std::nullopt;
    if ( ! current_txs_ || *current_txs_ != tmpl.selection.txs) {
        current_txs_ = std::make_shared<std::vector<transaction_const_ptr> const>(tmpl.selection.txs);
    }
    return build(*slot, tmpl, outputs);
}

share_outcome work_engine::submit(share_request const& share) {
    auto* slot = find(share.channel_id);
    if (slot == nullptr) return rejected_share(share, error_invalid_channel_id);
    bool const extended = slot->kind == channel_kind::extended;
    if (share.extended != extended) return rejected_share(share, error_invalid_channel_type);

    auto const job = std::find_if(slot->jobs.begin(), slot->jobs.end(),
        [&](work_engine::job const& item) { return item.id == share.job_id; });
    if (job == slot->jobs.end()) return rejected_share(share, error_invalid_job_id);
    if (job->stale || ! job->active) return rejected_share(share, error_stale_share);
    if (share.extranonce.size() != job->extranonce_size) {
        return rejected_share(share, error_invalid_extranonce_size);
    }
    if (share.ntime < job->ntime_start
        || uint64_t{share.ntime} > uint64_t{job->ntime_start} + ntime_roll_seconds) {
        return rejected_share(share, error_invalid_ntime);
    }
    auto const rolled = (share.version ^ job->version) & ~version_rolling_mask;
    if (job->version_rolling ? rolled != 0 : share.version != job->version) {
        return rejected_share(share, error_invalid_version);
    }

    data_chunk raw;
    raw.reserve(job->coinbase_tx_prefix.size() + job->extranonce_prefix.size()
        + share.extranonce.size() + job->coinbase_tx_suffix.size());
    raw.insert(raw.end(), job->coinbase_tx_prefix.begin(), job->coinbase_tx_prefix.end());
    raw.insert(raw.end(), job->extranonce_prefix.begin(), job->extranonce_prefix.end());
    raw.insert(raw.end(), share.extranonce.begin(), share.extranonce.end());
    raw.insert(raw.end(), job->coinbase_tx_suffix.begin(), job->coinbase_tx_suffix.end());
    byte_reader reader(raw);
    auto coinbase = domain::chain::transaction::from_data(reader, true);
    if ( ! coinbase || ! reader.is_exhausted()) {
        return rejected_share(share, error_invalid_extranonce_size);
    }

    // Standard jobs already fixed the extranonce, so the root stored at
    // activation is the one in the header. Extended jobs roll it.
    auto const root = extended
        ? fold_merkle(coinbase->hash(), job->merkle_path)
        : job->merkle_root;
    domain::chain::header const solved(share.version, job->prev_hash, root,
        share.ntime, job->bits, share.nonce);
    auto const digest = domain::chain::hash(solved);
    if (to_uint256(digest) > to_uint256(job->share_target)) {
        return rejected_share(share, error_difficulty_too_low);
    }

    share_key key{share.job_id, share.nonce, share.ntime, share.version, share.extranonce};
    if (std::find(slot->shares.begin(), slot->shares.end(), key) != slot->shares.end()) {
        return rejected_share(share, error_duplicate_share);
    }
    // Forgetting accepted keys would allow replay on this still-valid job.
    // Keep the history until replacement work makes all those keys stale.
    if (slot->shares.size() >= max_remembered_shares) {
        return rejected_share(share, error_share_limit);
    }
    slot->shares.push_back(std::move(key));

    share_outcome outcome;
    outcome.channel_id = share.channel_id;
    outcome.sequence_number = share.sequence_number;
    outcome.accepted_count = 1;
    outcome.shares_sum = share_difficulty(job->share_target);
    outcome.renew = slot->shares.size() == max_remembered_shares;
    auto const network = domain::chain::compact::from_compact(job->bits);
    if (network && to_uint256(digest) <= network->big()) {
        outcome.status = share_status::block;
        outcome.block = std::make_shared<domain::message::block>(
            assemble_block(solved, *coinbase, *job->txs));
        return outcome;
    }
    outcome.status = share_status::accepted;
    return outcome;
}

size_t work_engine::channel_count() const {
    return channels_.size();
}

bool work_engine::has_active_job() const {
    for (auto const& slot : channels_) {
        for (auto const& item : slot.jobs) {
            if (item.active && ! item.stale) return true;
        }
    }
    return false;
}

work_engine::channel* work_engine::find(uint32_t channel_id) {
    auto const it = std::find_if(channels_.begin(), channels_.end(),
        [channel_id](channel const& slot) { return slot.id == channel_id; });
    return it == channels_.end() ? nullptr : &*it;
}

} // namespace kth::node::mining
