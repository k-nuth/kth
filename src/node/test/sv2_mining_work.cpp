// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/node/mining/block_submit.hpp>
#include <kth/node/mining/work.hpp>

using namespace kth;
using namespace kth::node::mining;

namespace {

hash_digest maximum_target() {
    hash_digest target;
    target.fill(0xff);
    return target;
}

open_request request(uint32_t id = 0, channel_kind kind = channel_kind::standard) {
    open_request value;
    value.request_id = id;
    value.kind = kind;
    value.max_target = maximum_target();
    return value;
}

blockchain::mining_template mining_template() {
    blockchain::mining_template value{};
    value.version = 0x20000000u;
    value.height = 1;
    value.bits = 0x207fffffu;
    value.min_time = 100;
    value.current_time = 101;
    value.coinbase_value = 5000000000;
    value.size_limit = 32000000;
    value.sigchecks_limit = 640000;
    return value;
}

domain::chain::output::list outputs(uint64_t value) {
    short_hash hash{};
    hash.fill(0x42);
    return {{value, domain::chain::script(
        domain::chain::script::to_pay_public_key_hash_pattern(hash)), std::nullopt}};
}

domain::chain::transaction coinbase(work_job const& job, opened_channel const& channel,
    data_chunk const& extranonce = {}) {
    auto raw = job.coinbase_tx_prefix;
    raw.insert(raw.end(), channel.extranonce_prefix.begin(), channel.extranonce_prefix.end());
    raw.insert(raw.end(), extranonce.begin(), extranonce.end());
    raw.insert(raw.end(), job.coinbase_tx_suffix.begin(), job.coinbase_tx_suffix.end());
    byte_reader source(raw);
    auto const tx = domain::chain::transaction::from_data(source, true);
    REQUIRE(tx.has_value());
    REQUIRE(source.is_exhausted());
    return *tx;
}

share_request share(work_job const& job, uint32_t nonce = 0) {
    share_request value;
    value.extended = job.extended;
    value.channel_id = job.channel_id;
    value.job_id = job.job_id;
    value.nonce = nonce;
    value.ntime = job.ntime_start;
    value.version = job.version;
    return value;
}

void require_share_error(work_engine& engine, share_request const& value, std::string_view error) {
    auto const result = engine.submit(value);
    REQUIRE(result.status == share_status::rejected);
    REQUIRE(result.error_code == error);
    REQUIRE(result.channel_id == value.channel_id);
    REQUIRE(result.sequence_number == value.sequence_number);
    REQUIRE_FALSE(result.block);
}

void require_accepted(share_outcome const& result) {
    REQUIRE(result.status != share_status::rejected);
    REQUIRE(result.error_code.empty());
    REQUIRE(result.accepted_count == 1);
}

#if defined(KTH_CURRENCY_BCH)
std::string cash_address(uint8_t version, data_chunk const& hash,
    std::string_view prefix = "bchreg") {
    data_chunk bytes{version};
    bytes.insert(bytes.end(), hash.begin(), hash.end());
    data_chunk payload;
    uint32_t accumulator = 0;
    unsigned bits = 0;
    for (auto byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            payload.push_back(static_cast<uint8_t>((accumulator >> bits) & 31));
        }
    }
    if (bits != 0) payload.push_back(static_cast<uint8_t>((accumulator << (5 - bits)) & 31));
    return domain::wallet::cashaddr::encode(prefix, payload);
}
#endif

} // namespace

TEST_CASE("Mining payout is a supported cashaddr for the selected network", "[sv2 mining]") {
    std::string error;
#if defined(KTH_CURRENCY_BCH)
    short_hash hash{};
    hash.fill(0x42);
    data_chunk const hash20(hash.begin(), hash.end());
    for (auto const version : {0u, 8u, 16u, 24u}) {
        auto const value = payout_from_address(cash_address(version, hash20),
            domain::config::network::regtest, error);
        INFO(error);
        REQUIRE(value.has_value());
        auto const expected = domain::chain::script((version & 8) != 0
            ? domain::chain::script::to_pay_script_hash_pattern(hash)
            : domain::chain::script::to_pay_public_key_hash_pattern(hash));
        REQUIRE(value->script == expected);
        REQUIRE(error.empty());
    }
    hash_digest hash32{};
    hash32.fill(0x24);
    data_chunk const long_hash(hash32.begin(), hash32.end());
    for (auto const version : {11u, 27u}) {
        auto const value = payout_from_address(cash_address(version, long_hash),
            domain::config::network::regtest, error);
        REQUIRE(value.has_value());
        REQUIRE(value->script == domain::chain::script(
            domain::chain::script::to_pay_script_hash_32_pattern(hash32)));
    }
    auto uppercase = cash_address(0, hash20);
    std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(),
        [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    REQUIRE(payout_from_address(uppercase, domain::config::network::regtest, error));
    REQUIRE(payout_from_address(cash_address(0, hash20, "bitcoincash"),
        domain::config::network::mainnet, error));
    REQUIRE(payout_from_address(cash_address(0, hash20, "bchtest"),
        domain::config::network::chipnet, error));
    auto valid = cash_address(0, hash20);
    auto malformed = valid;
    malformed.back() = malformed.back() == 'q' ? 'p' : 'q';
    domain::wallet::payment_address const legacy(hash, domain::wallet::payment_address::testnet_p2kh);
    for (auto const& refused : {std::string{}, std::string("bchreg:q"), malformed,
            valid.substr(valid.find(':') + 1), legacy.encoded_legacy(),
            cash_address(0, hash20, "bchtest"), cash_address(0, hash20, "bitcoincash"),
            cash_address(3, long_hash), cash_address(32, hash20), cash_address(128, hash20),
            cash_address(4, data_chunk(40, 0x42))}) {
        INFO(refused);
        REQUIRE_FALSE(payout_from_address(refused, domain::config::network::regtest, error));
        REQUIRE_FALSE(error.empty());
    }
    auto [prefix, payload] = domain::wallet::cashaddr::decode(valid, "bchreg");
    payload.back() |= 1;
    REQUIRE_FALSE(payout_from_address(domain::wallet::cashaddr::encode(prefix, payload),
        domain::config::network::regtest, error));
    node::sv2_settings settings;
    REQUIRE(mining_startup_reason(settings, domain::config::network::regtest).empty());
    settings.mining.enabled = true;
    REQUIRE_FALSE(mining_startup_reason(settings, domain::config::network::regtest).empty());
    settings.enabled = true;
    REQUIRE_FALSE(mining_startup_reason(settings, domain::config::network::regtest).empty());
    settings.mining.address = "invalid";
    REQUIRE_FALSE(mining_startup_reason(settings, domain::config::network::regtest).empty());
    settings.mining.address = valid;
    REQUIRE(mining_startup_reason(settings, domain::config::network::regtest).empty());
#else
    REQUIRE_FALSE(payout_from_address("bitcoincash:q", domain::config::network::mainnet, error));
    REQUIRE_FALSE(error.empty());
#endif
}

TEST_CASE("Mining channels have disjoint extranonces and bounded opens", "[sv2 mining]") {
    work_engine engine(1);
    auto const standard = engine.open(request(1));
    auto extended_request = request(2, channel_kind::extended);
    extended_request.min_extranonce_size = 32;
    auto const extended = engine.open(extended_request);
    REQUIRE(standard.status == open_status::opened);
    REQUIRE(extended.status == open_status::opened);
    REQUIRE(standard.channel_id != extended.channel_id);
    REQUIRE(standard.group_channel_id != standard.channel_id);
    REQUIRE(standard.group_channel_id != extended.channel_id);
    REQUIRE(standard.extranonce_prefix.size() == extranonce_prefix_size);
    REQUIRE(standard.extranonce_prefix != extended.extranonce_prefix);
    REQUIRE(extended.extranonce_size == 32);
    REQUIRE(engine.channel_count() == 2);
    work_engine other(uint64_t{1} << 32 | 1);
    REQUIRE(other.open(request()).extranonce_prefix != standard.extranonce_prefix);
    REQUIRE(engine.open(request(1)).error_code == error_duplicate_request_id);
    auto unsupported = request(3, channel_kind::extended);
    unsupported.standard_only = true;
    REQUIRE(engine.open(unsupported).error_code == error_requires_standard_jobs);
    unsupported.request_id = 4;
    unsupported.standard_only = false;
    unsupported.min_extranonce_size = 33;
    REQUIRE(engine.open(unsupported).error_code == error_extranonce_too_large);
    for (uint32_t id = 5; engine.channel_count() < max_mining_channels; ++id) {
        REQUIRE(engine.open(request(id)).status == open_status::opened);
    }
    REQUIRE(engine.open(request(100)).error_code == error_too_many_channels);
    REQUIRE(engine.close(standard.channel_id));
    REQUIRE_FALSE(engine.close(standard.channel_id));
    REQUIRE(engine.close(extended.group_channel_id));
    REQUIRE(engine.open(request(101)).status == open_status::opened);

    work_engine requests(0);
    for (uint32_t id = 0; id < max_remembered_requests; ++id) {
        auto const opened = requests.open(request(id));
        REQUIRE(opened.status == open_status::opened);
        REQUIRE(requests.close(opened.channel_id));
    }
    REQUIRE(requests.open(request(max_remembered_requests)).status == open_status::protocol_error);
}

TEST_CASE("Mining targets track nominal hashes at all finite scales", "[sv2 mining]") {
    work_engine engine(0);
    auto value = request();
    REQUIRE(engine.open(value).target == maximum_target());
    for (auto const rate : {1.f, 1000000.f, 1000000000000000.f}) {
        ++value.request_id;
        value.nominal_hash_rate = rate;
        auto const opened = engine.open(value);
        REQUIRE(opened.status == open_status::opened);
        uint256_t const hashes = static_cast<uint64_t>(rate) * uint256_t{share_interval_seconds};
        REQUIRE(to_uint256(opened.target) == std::numeric_limits<uint256_t>::max() / hashes);
    }
    ++value.request_id;
    value.nominal_hash_rate = std::numeric_limits<float>::denorm_min();
    REQUIRE(engine.open(value).target == maximum_target());
    ++value.request_id;
    value.nominal_hash_rate = std::numeric_limits<float>::max();
    auto const huge = to_uint256(engine.open(value).target);
    REQUIRE(huge > 0);
    REQUIRE(huge < (uint256_t{1} << 128));
    ++value.request_id;
    value.nominal_hash_rate = 1;
    value.max_target = {};
    value.max_target[0] = 42;
    REQUIRE(engine.open(value).target == value.max_target);
    for (auto const invalid : {-0.f, -1.f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()}) {
        ++value.request_id;
        value.nominal_hash_rate = invalid;
        REQUIRE(engine.open(value).status == open_status::protocol_error);
        REQUIRE(engine.update(1, invalid, maximum_target()).protocol_error);
    }
    REQUIRE(engine.update(0, 1, maximum_target()).unknown);
}

TEST_CASE("Mining coinbase preserves the supplied payout split and merkle path", "[sv2 mining]") {
    work_engine engine(0x1234);
    auto const standard = engine.open(request(1));
    auto const extended = engine.open(request(2, channel_kind::extended));
    auto tmpl = mining_template();
    for (uint32_t id = 1; id <= 3; ++id) {
        auto selected_outputs = outputs(1);
        selected_outputs.front().set_token_data(domain::chain::make_fungible(null_hash, id));
        tmpl.selection.txs.push_back(std::make_shared<domain::message::transaction>(
            domain::chain::transaction{1, id, {}, std::move(selected_outputs)}));
    }
    auto split = outputs(tmpl.coinbase_value - 1);
    auto second = outputs(1);
    split.insert(split.end(), second.begin(), second.end());
    split.push_back({0, domain::chain::script(domain::chain::script::to_null_data_pattern(
        data_chunk{0x42})), std::nullopt});
    auto const jobs = engine.on_template(tmpl, split);
    REQUIRE(jobs.size() == 2);
    REQUIRE(engine.has_active_job());
    for (size_t i = 0; i < jobs.size(); ++i) {
        auto const& channel = i == 0 ? standard : extended;
        data_chunk const extra(channel.extranonce_size, 0x24);
        auto const tx = coinbase(jobs[i], channel, extra);
        REQUIRE(tx.outputs() == split);
        REQUIRE(tx.inputs().size() == 1);
        REQUIRE(tx.inputs().front().previous_output().is_null());
        REQUIRE(tx.inputs().front().sequence() == 0xffffffffu);
        REQUIRE(tx.serialized_size(true) >= 100);
        REQUIRE(tx.inputs().front().script().bytes().size() <= coinbase_script_limit);
        REQUIRE(tx.inputs().front().script().bytes().front() == 0x51);
        auto root = tx.hash();
        for (auto const& sibling : jobs[i].merkle_path) root = bitcoin_hash(build_chunk({root, sibling}));
        auto const block = assemble_block(domain::chain::header{}, tx, tmpl.selection.txs);
        REQUIRE(root == block.generate_merkle_root());
        if (i == 0) REQUIRE(root == jobs[i].merkle_root);
        auto const& prefix = jobs[i].coinbase_tx_prefix;
        auto const raw = to_data_chunk(tx, true);
        REQUIRE(raw[4] == 1);
        REQUIRE(std::equal(channel.extranonce_prefix.begin(), channel.extranonce_prefix.end(),
            raw.begin() + static_cast<std::ptrdiff_t>(prefix.size())));
        REQUIRE(std::equal(extra.begin(), extra.end(), raw.begin()
            + static_cast<std::ptrdiff_t>(prefix.size() + channel.extranonce_prefix.size())));
    }
}

TEST_CASE("Mining rejects malformed templates and coinbases before advertising work", "[sv2 mining]") {
    work_engine engine(0);
    auto const opened = engine.open(request());
    auto tmpl = mining_template();
    auto paid = outputs(tmpl.coinbase_value);
    REQUIRE_FALSE(engine.on_channel(0, tmpl, paid));
    SECTION("overflow target") { tmpl.bits = 0x23010000; }
    SECTION("zero target") { tmpl.bits = 0; }
    SECTION("time before MTP") { tmpl.current_time = tmpl.min_time - 1; }
    SECTION("coinbase does not fit") { tmpl.size_limit = 99; }
    SECTION("selection does not fit") { tmpl.selection.total_size = tmpl.size_limit; }
    SECTION("block header and transaction count do not fit") { tmpl.size_limit = 180; }
    SECTION("wrong payout total") { paid.front().set_value(tmpl.coinbase_value - 1); }
    SECTION("coinbase cannot mint CashTokens") {
        paid.front().set_token_data(domain::chain::make_fungible(null_hash, 1));
    }
    SECTION("overflow payout total") {
        paid = outputs(std::numeric_limits<uint64_t>::max());
        paid.push_back(outputs(1).front());
    }
    SECTION("no outputs") { tmpl.coinbase_value = 0; paid.clear(); }
    SECTION("too many outputs") { tmpl.coinbase_value = 0; paid.assign(257, outputs(0).front()); }
    SECTION("output bytes bound") {
        paid.front().set_script(domain::chain::script(data_chunk(65536, 0), false));
    }
    SECTION("suffix field bound") {
        paid.front().set_script(domain::chain::script(data_chunk(65516, 0), false));
    }
    SECTION("height range") { tmpl.height = std::numeric_limits<size_t>::max(); }
    SECTION("height push field bound") { tmpl.height = uint64_t{1} << 55; }
    REQUIRE_FALSE(engine.on_channel(opened.channel_id, tmpl, paid));
    REQUIRE_FALSE(engine.has_active_job());
}

TEST_CASE("Mining replacement jobs give fresh hash space and invalidate old work even on failure", "[sv2 mining]") {
    for (auto const kind : {channel_kind::standard, channel_kind::extended}) {
        work_engine engine(0);
        auto const opened = engine.open(request(0, kind));
        auto tmpl = mining_template();
        auto const paid = outputs(tmpl.coinbase_value);
        auto const first = engine.on_channel(opened.channel_id, tmpl, paid);
        REQUIRE(first.has_value());
        auto value = share(*first);
        value.extranonce.resize(opened.extranonce_size, 0x42);
        require_accepted(engine.submit(value));
        auto const next = engine.on_channel(opened.channel_id, tmpl, paid);
        REQUIRE(next.has_value());
        REQUIRE(next->coinbase_tx_prefix != first->coinbase_tx_prefix);
        REQUIRE(next->merkle_root != first->merkle_root);
        require_share_error(engine, value, error_stale_share);
        value.job_id = next->job_id;
        require_accepted(engine.submit(value));

        tmpl.bits = 0;
        REQUIRE(engine.on_template(tmpl, paid).empty());
        REQUIRE_FALSE(engine.has_active_job());
        require_share_error(engine, value, error_stale_share);
    }
}

TEST_CASE("Mining block size includes the header and transaction count", "[sv2 mining]") {
    work_engine engine(0);
    auto const opened = engine.open(request());
    auto tmpl = mining_template();
    auto const paid = outputs(tmpl.coinbase_value);
    auto const initial = engine.on_channel(opened.channel_id, tmpl, paid);
    REQUIRE(initial.has_value());
    auto const tx = coinbase(*initial, opened);
    auto const block = assemble_block(domain::chain::header{}, tx, {});
    tmpl.size_limit = static_cast<domain::chain::block const&>(block).serialized_size();
    REQUIRE(engine.on_channel(opened.channel_id, tmpl, paid));
    --tmpl.size_limit;
    REQUIRE_FALSE(engine.on_channel(opened.channel_id, tmpl, paid));
    REQUIRE_FALSE(engine.has_active_job());
}

TEST_CASE("Mining coinbase preserves the bounded output list across CompactSize encoding", "[sv2 mining]") {
    work_engine engine(0);
    auto const opened = engine.open(request());
    auto const tmpl = mining_template();
    domain::chain::output::list paid(256, outputs(0).front());
    paid.front().set_value(tmpl.coinbase_value);
    auto const job = engine.on_channel(opened.channel_id, tmpl, paid);
    REQUIRE(job.has_value());
    REQUIRE(coinbase(*job, opened).outputs() == paid);
}

TEST_CASE("Mining shares enforce channel job extranonce time version and target", "[sv2 mining]") {
    work_engine engine(0);
    auto const standard = engine.open(request(1));
    auto const extended = engine.open(request(2, channel_kind::extended));
    auto const tmpl = mining_template();
    auto const jobs = engine.on_template(tmpl, outputs(tmpl.coinbase_value));
    REQUIRE(jobs.size() == 2);
    auto value = share(jobs.front());
    value.sequence_number = 42;
    SECTION("unknown channel") { value.channel_id = 0; require_share_error(engine, value, error_invalid_channel_id); }
    SECTION("wrong channel type") { value.extended = true; require_share_error(engine, value, error_invalid_channel_type); }
    SECTION("unknown job") { value.job_id = 0; require_share_error(engine, value, error_invalid_job_id); }
    SECTION("standard extranonce") { value.extranonce = {0}; require_share_error(engine, value, error_invalid_extranonce_size); }
    SECTION("extended extranonce") {
        value = share(jobs.back());
        value.extranonce.resize(extended.extranonce_size - 1);
        require_share_error(engine, value, error_invalid_extranonce_size);
    }
    SECTION("early ntime") { --value.ntime; require_share_error(engine, value, error_invalid_ntime); }
    SECTION("late ntime") { value.ntime += ntime_roll_seconds + 1; require_share_error(engine, value, error_invalid_ntime); }
    SECTION("reserved version bit") { value.version ^= 1; require_share_error(engine, value, error_invalid_version); }
    SECTION("version base bit") { value.version ^= 0x20000000u; require_share_error(engine, value, error_invalid_version); }
    SECTION("rolling bits and ntime endpoints") {
        value.version ^= version_rolling_mask;
        require_accepted(engine.submit(value));
        value.ntime += ntime_roll_seconds;
        require_accepted(engine.submit(value));
    }
    SECTION("duplicate excludes sequence number") {
        require_accepted(engine.submit(value));
        ++value.sequence_number;
        require_share_error(engine, value, error_duplicate_share);
    }
    SECTION("low difficulty") {
        auto target = request(3);
        target.max_target = {};
        auto const hard = engine.open(target);
        auto const job = engine.on_channel(hard.channel_id, tmpl, outputs(tmpl.coinbase_value));
        REQUIRE(job.has_value());
        require_share_error(engine, share(*job), error_difficulty_too_low);
    }
}

TEST_CASE("Mining share target remains fixed for active jobs across updates", "[sv2 mining]") {
    work_engine engine(0);
    auto const opened = engine.open(request());
    auto const tmpl = mining_template();
    auto const job = engine.on_channel(opened.channel_id, tmpl, outputs(tmpl.coinbase_value));
    REQUIRE(job.has_value());
    REQUIRE_FALSE(engine.update(opened.channel_id, 0, maximum_target()).target);
    auto const changed = engine.update(opened.channel_id, 0, {});
    REQUIRE(changed.target.has_value());
    REQUIRE(*changed.target == null_hash);
    require_accepted(engine.submit(share(*job)));
    auto const next = engine.on_channel(opened.channel_id, tmpl, outputs(tmpl.coinbase_value));
    REQUIRE(next.has_value());
    require_share_error(engine, share(*next), error_difficulty_too_low);
    require_share_error(engine, share(*job), error_stale_share);
}

TEST_CASE("Mining duplicate history is bounded without accepting replays", "[sv2 mining]") {
    work_engine engine(0);
    auto const opened = engine.open(request());
    auto tmpl = mining_template();
    auto const job = engine.on_channel(opened.channel_id, tmpl, outputs(tmpl.coinbase_value));
    REQUIRE(job.has_value());
    for (uint32_t nonce = 0; nonce < max_remembered_shares; ++nonce) {
        auto const outcome = engine.submit(share(*job, nonce));
        require_accepted(outcome);
        REQUIRE(outcome.renew == (nonce + 1 == max_remembered_shares));
    }
    require_share_error(engine, share(*job), error_duplicate_share);
    require_share_error(engine, share(*job, max_remembered_shares), error_share_limit);
    engine.stale_all();
    REQUIRE_FALSE(engine.has_active_job());
    require_share_error(engine, share(*job), error_stale_share);
    tmpl.previous_block_hash[0] = 1;
    auto const next = engine.on_channel(opened.channel_id, tmpl, outputs(tmpl.coinbase_value));
    REQUIRE(next.has_value());
    require_accepted(engine.submit(share(*next)));
    for (size_t count = 0; count < max_jobs_per_channel; ++count) {
        REQUIRE(engine.on_channel(opened.channel_id, tmpl, outputs(tmpl.coinbase_value)));
    }
    require_share_error(engine, share(*job), error_invalid_job_id);
}

TEST_CASE("Mining network target shares assemble the complete BCH block", "[sv2 mining]") {
    for (auto const kind : {channel_kind::standard, channel_kind::extended}) {
        work_engine engine(0);
        auto const opened = engine.open(request(0, kind));
        auto tmpl = mining_template();
        auto const tx = std::make_shared<domain::message::transaction>(
            domain::chain::transaction{1, 42, {}, {}});
        tmpl.selection.txs.push_back(tx);
        auto const paid = outputs(tmpl.coinbase_value);
        auto const job = engine.on_channel(opened.channel_id, tmpl, paid);
        REQUIRE(job.has_value());
        auto value = share(*job);
        value.extranonce.resize(opened.extranonce_size, 0x42);
        auto const cb = coinbase(*job, opened, value.extranonce);
        auto const root = bitcoin_hash(build_chunk({cb.hash(), tx->hash()}));
        auto const network = domain::chain::compact::from_compact(tmpl.bits);
        REQUIRE(network.has_value());
        bool found_block = false;
        bool found_share = false;
        for (uint32_t nonce = 0; nonce < 256 && (! found_block || ! found_share); ++nonce) {
            value.nonce = nonce;
            domain::chain::header const header(value.version, tmpl.previous_block_hash, root,
                value.ntime, tmpl.bits, nonce);
            auto const is_block = to_uint256(domain::chain::hash(header)) <= network->big();
            auto const result = engine.submit(value);
            require_accepted(result);
            REQUIRE(result.shares_sum == 0);
            if (is_block) {
                REQUIRE(result.status == share_status::block);
                REQUIRE(result.block);
                REQUIRE(result.block->header() == header);
                REQUIRE(result.block->transactions().size() == 2);
                REQUIRE(result.block->transactions().front() == cb);
                REQUIRE(result.block->transactions().back() == static_cast<domain::chain::transaction const&>(*tx));
                REQUIRE(result.block->is_valid_merkle_root());
                found_block = true;
            } else {
                REQUIRE(result.status == share_status::accepted);
                REQUIRE_FALSE(result.block);
                found_share = true;
            }
        }
        REQUIRE(found_block);
        REQUIRE(found_share);
    }
}
