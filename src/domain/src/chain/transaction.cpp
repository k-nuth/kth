// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/transaction.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>

#if defined(KTH_CURRENCY_BCH)
#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#endif

using namespace kth::infrastructure::machine;
using namespace kth::domain::machine;

namespace kth::domain::chain {

// Constructors.
//-----------------------------------------------------------------------------

transaction::transaction(uint32_t version, uint32_t locktime, input::list inputs, output::list outputs)
    : version_(version)
    , locktime_(locktime)
    , inputs_(std::move(inputs))
    , outputs_(std::move(outputs))
{}

// static
transaction transaction::null() {
    return transaction{};
}

bool transaction::is_null() const {
    return version_ == 0
        && locktime_ == 0
        && inputs_.empty()
        && outputs_.empty();
}

// Operators.
//-----------------------------------------------------------------------------

bool transaction::operator==(transaction const& x) const {
    return version_ == x.version_
        && locktime_ == x.locktime_
        && inputs_ == x.inputs_
        && outputs_ == x.outputs_;
}

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<transaction> transaction::from_data(byte_reader& reader, bool wire /*= true*/) {
    if (wire) {
        auto const version = reader.read_little_endian<uint32_t>();
        if ( ! version) {
            return std::unexpected(version.error());
        }
        auto inputs = read_collection<chain::input>(reader, wire);
        if ( ! inputs) {
            return std::unexpected(inputs.error());
        }
        auto outputs = read_collection<chain::output>(reader, wire);
        if ( ! outputs) {
            return std::unexpected(outputs.error());
        }
        auto const locktime = reader.read_little_endian<uint32_t>();
        if ( ! locktime) {
            return std::unexpected(locktime.error());
        }
        return transaction {
            *version,
            *locktime,
            std::move(*inputs),
            std::move(*outputs)
        };
    }

    // Database (outputs forward) serialization.
    auto outputs = read_collection<chain::output>(reader, wire);
    if ( ! outputs) {
        return std::unexpected(outputs.error());
    }
    auto inputs = read_collection<chain::input>(reader, wire);
    if ( ! inputs) {
        return std::unexpected(inputs.error());
    }
    auto const locktime = reader.read_variable_little_endian();
    if ( ! locktime) {
        return std::unexpected(locktime.error());
    }
    auto const version = reader.read_variable_little_endian();
    if ( ! version) {
        return std::unexpected(version.error());
    }

    if (*locktime > max_uint32 || *version > max_uint32) {
        return std::unexpected(error::invalid_size);
    }

    return transaction {
        uint32_t(*version),
        uint32_t(*locktime),
        std::move(*inputs),
        std::move(*outputs)
    };
}

expect<void> transaction::to_data(byte_writer& writer, bool wire) const {
    if (wire) {
        if (auto r = writer.write_little_endian<uint32_t>(version_); ! r) return r;
        if (auto r = detail::write(writer, inputs_, wire); ! r) return r;
        if (auto r = detail::write(writer, outputs_, wire); ! r) return r;
        return writer.write_little_endian<uint32_t>(locktime_);
    }
    // Database (outputs forward) serialization.
    if (auto r = detail::write(writer, outputs_, wire); ! r) return r;
    if (auto r = detail::write(writer, inputs_, wire); ! r) return r;
    if (auto r = writer.write_variable_little_endian(locktime_); ! r) return r;
    return writer.write_variable_little_endian(version_);
}

// Size.
//-----------------------------------------------------------------------------

size_t transaction::serialized_size(bool wire) const {
    auto const ins = [wire](size_t size, input const& input) {
        return size + input.serialized_size(wire);
    };

    auto const outs = [wire](size_t size, output const& output) {
        return size + output.serialized_size(wire);
    };

    return
        (wire ? sizeof(version_) : infrastructure::message::variable_uint_size(version_))
        + (wire ? sizeof(locktime_) : infrastructure::message::variable_uint_size(locktime_))
        + infrastructure::message::variable_uint_size(inputs_.size())
        + infrastructure::message::variable_uint_size(outputs_.size())
        + std::accumulate(inputs_.begin(), inputs_.end(), size_t{0}, ins)
        + std::accumulate(outputs_.begin(), outputs_.end(), size_t{0}, outs)
        ;
}

// Accessors.
//-----------------------------------------------------------------------------

uint32_t transaction::version() const { return version_; }
uint32_t transaction::locktime() const { return locktime_; }
input::list const& transaction::inputs() const { return inputs_; }
output::list const& transaction::outputs() const { return outputs_; }

// Hashes — recomputed on every call (no cache).
//-----------------------------------------------------------------------------

hash_digest transaction::hash() const {
    return chain::hash(*this);
}

hash_digest transaction::outputs_hash() const {
    return chain::to_outputs(*this);
}

hash_digest transaction::inpoints_hash() const {
    return chain::to_inpoints(*this);
}

hash_digest transaction::sequences_hash() const {
    return chain::to_sequences(*this);
}

hash_digest transaction::utxos_hash() const {
    return chain::to_utxos(*this);
}

// Validation helpers.
//-----------------------------------------------------------------------------

bool transaction::is_coinbase() const {
    return inputs_.size() == 1 && inputs_.front().previous_output().is_null();
}

bool transaction::is_oversized_coinbase() const {
    if ( ! is_coinbase()) {
        return false;
    }

    auto const script_size = inputs_.front().script().serialized_size(false);
    return script_size < min_coinbase_size || script_size > max_coinbase_size;
}

bool transaction::is_null_non_coinbase() const {
    if (is_coinbase()) {
        return false;
    }

    auto const invalid = [](input const& input) {
        return input.previous_output().is_null();
    };

    return std::any_of(inputs_.begin(), inputs_.end(), invalid);
}

// private
bool transaction::all_inputs_final() const {
    auto const finalized = [](input const& input) {
        return input.is_final();
    };

    return std::all_of(inputs_.begin(), inputs_.end(), finalized);
}

bool transaction::is_final(size_t block_height, uint32_t block_time) const {
    auto const max_locktime = [=, this]() {
        return locktime_ < locktime_threshold ? *safe_unsigned<uint32_t>(block_height) : block_time;
    };

    return locktime_ == 0 || locktime_ < max_locktime() || all_inputs_final();
}

bool transaction::is_locked(size_t block_height, uint32_t median_time_past) const {
    if (version_ < relative_locktime_min_version || is_coinbase()) {
        return false;
    }

    auto const locked = [block_height, median_time_past](input const& input) {
        return input.is_locked(block_height, median_time_past);
    };

    return std::any_of(inputs_.begin(), inputs_.end(), locked);
}

// This is not a consensus rule, just detection of an irrational use.
bool transaction::is_locktime_conflict() const {
    return locktime_ != 0 && all_inputs_final();
}

size_t transaction::signature_operations(bool bip16, bool bip141) const {
    bip141 = false;  // No segwit
    auto const in = [bip16, bip141](size_t total, input const& input) {
        return ceiling_add(total, input.signature_operations(bip16, bip141));
    };

    auto const out = [bip141](size_t total, output const& output) {
        return ceiling_add(total, output.signature_operations(bip141));
    };

    return std::accumulate(inputs_.begin(), inputs_.end(), size_t{0}, in) +
           std::accumulate(outputs_.begin(), outputs_.end(), size_t{0}, out);
}

size_t transaction::signature_operations() const {
    auto const state = validation.state;
    auto const bip16 = state ? state->is_enabled(kth::domain::machine::script_flags::bip16_rule) : true;
    auto const bip141 = false;
    return signature_operations(bip16, bip141);
}

bool transaction::is_missing_previous_outputs() const {
    auto const missing = [](input const& input) {
        auto const& prevout = input.previous_output();
        auto const coinbase = prevout.is_null();
        auto const missing = !prevout.validation.cache.is_valid();
        return missing && !coinbase;
    };

    return std::any_of(inputs_.begin(), inputs_.end(), missing);
}

point::list transaction::previous_outputs() const {
    point::list prevouts;
    prevouts.reserve(inputs_.size());
    auto const pointer = [&prevouts](input const& input) {
        prevouts.push_back(input.previous_output());
    };

    std::for_each(inputs_.begin(), inputs_.end(), pointer);
    return prevouts;
}

point::list transaction::missing_previous_outputs() const {
    point::list prevouts;
    prevouts.reserve(inputs_.size());
    auto const accumulator = [&prevouts](input const& input) {
        auto const& prevout = input.previous_output();
        auto const missing = !prevout.validation.cache.is_valid();

        if (missing && !prevout.is_null()) {
            prevouts.push_back(prevout);
        }
    };

    std::for_each(inputs_.begin(), inputs_.end(), accumulator);
    prevouts.shrink_to_fit();
    return prevouts;
}

hash_list transaction::missing_previous_transactions() const {
    auto const points = missing_previous_outputs();
    hash_list hashes;
    hashes.reserve(points.size());
    auto const hasher = [&hashes](output_point const& point) {
        return point.hash();
    };

    std::for_each(points.begin(), points.end(), hasher);
    return distinct(hashes);
}

bool transaction::is_internal_double_spend() const {
    auto prevouts = previous_outputs();
    std::sort(prevouts.begin(), prevouts.end());
    auto const distinct_end = std::unique(prevouts.begin(), prevouts.end());
    auto const distinct = (distinct_end == prevouts.end());
    return !distinct;
}

bool transaction::is_double_spend(bool include_unconfirmed) const {
    auto const spent = [include_unconfirmed](input const& input) {
        auto const& prevout = input.previous_output().validation;
        return prevout.spent && (include_unconfirmed || prevout.confirmed);
    };

    return std::any_of(inputs_.begin(), inputs_.end(), spent);
}

bool transaction::is_dusty(uint64_t minimum_output_value) const {
    auto const dust = [minimum_output_value](output const& output) {
        return output.is_dust(minimum_output_value);
    };

    return std::any_of(outputs_.begin(), outputs_.end(), dust);
}

bool transaction::is_mature(size_t height) const {
    auto const mature = [height](input const& input) {
        return input.previous_output().is_mature(height);
    };

    return std::all_of(inputs_.begin(), inputs_.end(), mature);
}

uint64_t transaction::total_input_value() const {
    return chain::total_input_value(*this);
}

uint64_t transaction::total_output_value() const {
    return chain::total_output_value(*this);
}

uint64_t transaction::fees() const {
    return floor_subtract(total_input_value(), total_output_value());
}

bool transaction::is_overspent() const {
    return ! is_coinbase() && total_output_value() > total_input_value();
}

// Validation.
//-----------------------------------------------------------------------------

code transaction::check(size_t max_block_size, bool transaction_pool, bool retarget) const {
    if (inputs_.empty() || outputs_.empty()) {
        return error::empty_transaction;
    }

    if (is_null_non_coinbase()) {
        return error::previous_output_null;
    }

    if (total_output_value() > max_satoshi_supply) {
        return error::spend_overflow;
    }

    if ( ! transaction_pool && is_oversized_coinbase()) {
        return error::invalid_coinbase_script_size;
    }

    if (transaction_pool && is_coinbase()) {
        return error::coinbase_transaction;
    }

    if (transaction_pool && is_internal_double_spend()) {
        return error::transaction_internal_double_spend;
    }

    if (transaction_pool && serialized_size(true) >= max_block_size) {
        return error::transaction_size_limit;
    }

    return error::success;
}

size_t transaction::min_tx_size(script_flags_t flags) const {
    auto const is_enabled = [](script_flags_t active, auto flag) {
        return script::is_enabled(active, flag);
    };
#if defined(KTH_CURRENCY_BCH)
    if (is_enabled(flags, kth::domain::machine::script_flags::bch_tokens)) {
        return min_transaction_size_descartes;      // 2023-May-15 (descartes)
    }
    if (is_enabled(flags, kth::domain::machine::script_flags::bch_cleanstack)) {
        return min_transaction_size_euclid;         // 2018-Nov-15 (euclid)
    }
#endif
    return 0;
}

code transaction::accept(
    script_flags_t flags,
    size_t height,
    uint32_t median_time_past,
    size_t max_sigops,
    bool is_under_checkpoint,
    bool transaction_pool
) const {
    auto const is_enabled = [](script_flags_t active, auto flag) {
        return script::is_enabled(active, flag);
    };

    auto const bip16 = is_enabled(flags, kth::domain::machine::script_flags::bip16_rule);
    auto const bip30 = is_enabled(flags, kth::domain::machine::script_flags::bip30_rule);
    auto const bip68 = is_enabled(flags, kth::domain::machine::script_flags::bip68_rule);
    auto const bip141 = false;  // No segwit
    auto const revert_bip30 = is_enabled(flags, kth::domain::machine::script_flags::allow_collisions);

    if (transaction_pool && is_under_checkpoint) {
        return error::premature_validation;
    }

#if defined(KTH_CURRENCY_BCH)
    if (serialized_size(true) < min_tx_size(flags)) {
        return error::transaction_size_limit;
    }

    if (is_enabled(flags, kth::domain::machine::script_flags::bch_tokens)) {
        if (version_ < 1 || version_ > 2) {
            return error::invalid_tx_version;
        }
    }
#endif

    if (transaction_pool && ! is_final(height, median_time_past)) {
        return error::transaction_non_final;
    }

    if (bip30 && ! revert_bip30 && validation.duplicate) {
        return error::unspent_duplicate;
    }

    if (is_missing_previous_outputs()) {
        return error::missing_previous_output;
    }

    for (auto const& in : inputs_) {
        auto const& cache = in.previous_output().validation.cache;
        if (cache.is_valid() && cache.value() > max_satoshi_supply) {
            return error::spend_overflow;
        }
    }

    if (is_double_spend(transaction_pool)) {
        return error::double_spend;
    }

    if ( ! is_mature(height)) {
        return error::coinbase_maturity;
    }

    if (is_overspent()) {
        return error::spend_exceeds_value;
    }

    if (bip68 && is_locked(height, median_time_past)) {
        return error::sequence_locked;
    }

#if defined(KTH_CURRENCY_BCH)
    if ( ! is_enabled(flags, kth::domain::machine::script_flags::bch_enforce_sigchecks)) {
#endif
        if (transaction_pool && signature_operations(bip16, bip141) > max_sigops) {
            return error::transaction_embedded_sigop_limit;
        }
#if defined(KTH_CURRENCY_BCH)
    }

    if (is_enabled(flags, kth::domain::machine::script_flags::bch_tokens)) {
        if (version_ > transaction_version_max || version_ < transaction_version_min) {
            return error::transaction_version_out_of_range;
        }
    }

    {
        auto const ec = validate_tokens(flags);
        if (ec != error::success) {
            return ec;
        }
    }
#endif

    return error::success;
}

// Coinbase transactions return success, to simplify iteration.
code transaction::connect_input(chain_state const& state, size_t input_index) const {
    if (input_index >= inputs_.size()) {
        return error::operation_failed;
    }

    if (is_coinbase()) {
        return error::success;
    }

    auto const& prevout = inputs_[input_index].previous_output().validation;

    if ( ! prevout.cache.is_valid()) {
        return error::missing_previous_output;
    }

    auto const flags = state.enabled_flags();
    auto const index32 = uint32_t(input_index);

    return verify(*this, index32, flags);
}

code transaction::connect() const {
    auto const state = validation.state;
    return state ? connect(*state) : error::operation_failed;
}

code transaction::connect(chain_state const& state) const {
    code ec;

    for (size_t input = 0; input < inputs_.size(); ++input) {
        if ((ec = connect_input(state, input))) {
            return ec;
        }
    }

    return error::success;
}

bool transaction::is_standard() const {
    for (auto const& in : inputs_) {
        if (in.script().pattern() == script_pattern::non_standard) {
            return false;
        }
    }

    return std::all_of(outputs_.begin(), outputs_.end(), [](auto const& out){
        return out.script().pattern() != script_pattern::non_standard;
    });
}

bool transaction::is_standard(script_flags_t flags) const {
    for (auto const& in : inputs_) {
        if (in.script().input_pattern() == script_pattern::non_standard) {
            return false;
        }
    }

    return std::all_of(outputs_.begin(), outputs_.end(), [flags](auto const& out){
        return out.script().output_pattern(flags) != script_pattern::non_standard;
    });
}

// Free functions.
//-----------------------------------------------------------------------------

hash_digest hash(transaction const& tx) {
    return bitcoin_hash(kth::to_data_chunk(tx, true));
}

hash_digest outputs_hash(transaction const& tx) {
    return to_outputs(tx);
}

hash_digest inpoints_hash(transaction const& tx) {
    return to_inpoints(tx);
}

hash_digest sequences_hash(transaction const& tx) {
    return to_sequences(tx);
}

hash_digest utxos_hash(transaction const& tx) {
    return to_utxos(tx);
}

namespace {

// Hash the concatenation of `obj.to_data(writer, wire=true)` over `range`.
// Hoisted out of every BIP143 hash helper that used to repeat the same
// allocate-buffer-write-loop boilerplate. `proj` extracts the inner
// `Serializable` object from each input element.
template <typename Range, typename Proj>
hash_digest hash_concat(Range const& range, Proj proj) {
    size_t size = 0;
    for (auto const& item : range) {
        auto const& obj = proj(item);
        size += obj.serialized_size();
    }
    data_chunk data(size);
    byte_writer writer(data);
    for (auto const& item : range) {
        auto const& obj = proj(item);
        auto const r = obj.to_data(writer, true);
        KTH_CONTRACT(r.has_value());
    }
    KTH_CONTRACT(writer.position() == data.size());
    return bitcoin_hash(data);
}

} // namespace

hash_digest to_outputs(transaction const& tx) {
    return hash_concat(tx.outputs(), [](output const& o) -> output const& { return o; });
}

hash_digest to_inpoints(transaction const& tx) {
    return hash_concat(tx.inputs(),
        [](input const& i) -> output_point const& { return i.previous_output(); });
}

hash_digest to_sequences(transaction const& tx) {
    auto const& ins = tx.inputs();
    auto const size = ins.size() * sizeof(uint32_t);
    data_chunk data(size);
    byte_writer writer(data);
    for (auto const& i : ins) {
        auto const r = writer.write_little_endian<uint32_t>(i.sequence());
        KTH_CONTRACT(r.has_value());
    }
    KTH_CONTRACT(writer.position() == data.size());
    return bitcoin_hash(data);
}

hash_digest to_utxos(transaction const& tx) {
    auto const& ins = tx.inputs();
    size_t size = 0;
    for (auto const& i : ins) {
        auto const& prevout = i.previous_output().validation.cache;
        if (prevout.is_valid()) size += prevout.serialized_size();
    }
    data_chunk data(size);
    byte_writer writer(data);
    for (auto const& i : ins) {
        auto const& prevout = i.previous_output().validation.cache;
        if ( ! prevout.is_valid()) continue;
        auto const r = prevout.to_data(writer, true);
        KTH_CONTRACT(r.has_value());
    }
    KTH_CONTRACT(writer.position() == data.size());
    return bitcoin_hash(data);
}

uint64_t total_input_value(transaction const& tx) {
    auto const sum = [](uint64_t total, input const& input) {
        auto const& prevout = input.previous_output().validation.cache;
        auto const missing = !prevout.is_valid();
        return ceiling_add(total, missing ? 0 : prevout.value());
    };

    return std::accumulate(tx.inputs().begin(), tx.inputs().end(), uint64_t(0), sum);
}

uint64_t total_output_value(transaction const& tx) {
    auto const sum = [](uint64_t total, output const& output) {
        return ceiling_add(total, output.value());
    };

    return std::accumulate(tx.outputs().begin(), tx.outputs().end(), uint64_t(0), sum);
}

uint64_t fees(transaction const& tx) {
    return floor_subtract(total_input_value(tx), total_output_value(tx));
}

bool is_overspent(transaction const& tx) {
    return ! tx.is_coinbase() && total_output_value(tx) > total_input_value(tx);
}

#if defined(KTH_CURRENCY_BCH)

code transaction::validate_tokens(script_flags_t flags) const {
    if ( ! script::is_enabled(flags, machine::script_flags::bch_tokens)) {
        if ( ! is_coinbase()) {
            for (auto const& in : inputs_) {
                auto const& cache = in.previous_output().validation.cache;
                if (cache.is_valid() && cache.token_data().has_value()) {
                    return error::token_pre_activation_input;
                }
            }
        }
        return error::success;
    }

    if (is_coinbase()) {
        for (auto const& out : outputs_) {
            if (out.token_data().has_value()) {
                return error::token_coinbase_has_tokens;
            }
        }
        return error::success;
    }

    boost::unordered_flat_set<token_id_t> potential_genesis_ids;
    boost::unordered_flat_map<token_id_t, int64_t> input_amounts_by_category;
    boost::unordered_flat_map<token_id_t, int64_t> genesis_amounts_by_category;

    boost::unordered_flat_set<token_id_t> input_minting_ids;
    boost::unordered_flat_map<token_id_t, size_t> input_mutables;

    struct nft_key {
        token_id_t id;
        commitment_t commitment;
        bool operator==(nft_key const&) const = default;
    };
    struct nft_key_hash {
        size_t operator()(nft_key const& k) const {
            auto seed = boost::hash<token_id_t>{}(k.id);
            boost::hash_combine(seed, boost::hash_range(k.commitment.begin(), k.commitment.end()));
            return seed;
        }
    };
    boost::unordered_flat_map<nft_key, size_t, nft_key_hash> input_immutables;

    for (size_t i = 0; i < inputs_.size(); ++i) {
        auto const& in = inputs_[i];
        auto const& prevout = in.previous_output();

        if (prevout.index() == 0) {
            potential_genesis_ids.insert(prevout.hash());
        }

        auto const& utxo = inputs_[i].previous_output().validation.cache;
        if ( ! utxo.is_valid()) continue;
        auto const& td_opt = utxo.token_data();

        if ( ! td_opt.has_value()) {
            continue;
        }

        auto const& td = td_opt.value();

        {
            int64_t const a = get_amount(td);
            if (a < 0) return error::token_amount_negative;
            if (a == 0 && is_fungible_only(td)) return error::token_fungible_only_amount_zero;
            if (has_nft(td)) {
                if (get_nft(td).commitment.size() > max_token_commitment_length(flags)) {
                    return error::token_commitment_oversized;
                }
            }
        }

        auto const& id = td.id;
        int64_t const amt = get_amount(td);

        auto& cat_amount = input_amounts_by_category[id];
        if (auto const r = kth::safe_add(cat_amount, amt); ! r) {
            return error::token_amount_overflow;
        } else {
            cat_amount = *r;
        }

        if (has_nft(td)) {
            if (is_immutable_nft(td)) {
                ++input_immutables[nft_key{id, get_nft(td).commitment}];
            } else if (is_mutable_nft(td)) {
                ++input_mutables[id];
            } else if (is_minting_nft(td)) {
                input_minting_ids.insert(id);
            }
        }
    }

    for (auto const& out : outputs_) {
        auto const& td_opt = out.token_data();
        if ( ! td_opt.has_value()) {
            continue;
        }

        auto const& td = td_opt.value();

        {
            int64_t const a = get_amount(td);
            if (a < 0) return error::token_amount_negative;
            if (a == 0 && is_fungible_only(td)) return error::token_fungible_only_amount_zero;
            if (has_nft(td)) {
                if (get_nft(td).commitment.size() > max_token_commitment_length(flags)) {
                    return error::token_commitment_oversized;
                }
            }
        }

        auto const& id = td.id;
        int64_t const amt = get_amount(td);

        int64_t* tally = nullptr;
        bool is_genesis = false;

        if (auto it = input_amounts_by_category.find(id); it != input_amounts_by_category.end()) {
            tally = &it->second;
        }

        if (potential_genesis_ids.contains(id)) {
            if (tally != nullptr) {
                return error::token_duplicate_genesis;
            }
            tally = &genesis_amounts_by_category[id];
            is_genesis = true;
        }

        if (tally == nullptr) {
            return error::token_invalid_category;
        }

        if (is_genesis) {
            if (auto const r = kth::safe_add(*tally, amt); ! r) {
                return error::token_amount_overflow;
            } else {
                *tally = *r;
            }
        } else {
            if (auto const r = kth::safe_subtract(*tally, amt); ! r) {
                return error::token_amount_overflow;
            } else {
                *tally = *r;
            }
        }

        if (*tally < 0) {
            return error::token_fungible_insufficient;
        }

        if (has_nft(td) && ! is_genesis) {
            bool found = false;

            if (is_immutable_nft(td)) {
                auto const key = nft_key{id, get_nft(td).commitment};
                if (auto it = input_immutables.find(key); it != input_immutables.end() && it->second > 0) {
                    --it->second;
                    found = true;
                }
            }

            if ( ! found && ! is_minting_nft(td)) {
                if (auto it = input_mutables.find(id); it != input_mutables.end() && it->second > 0) {
                    --it->second;
                    found = true;
                }
            }

            if ( ! found) {
                if (input_minting_ids.contains(id)) {
                    found = true;
                }
            }

            if ( ! found) {
                return error::token_nft_ex_nihilo;
            }
        }
    }

    return error::success;
}

#endif // KTH_CURRENCY_BCH

// Verify (script execution).
//-----------------------------------------------------------------------------

// A witness program is: version_byte (OP_0 or OP_1..OP_16) + push_byte + program_data.
static
bool is_witness_program(data_chunk const& bytes) {
    using machine::opcode;
    auto const size = bytes.size();
    if (size < min_witness_program_script || size > max_witness_program_script) return false;

    auto const version = bytes[0];
    if (version != static_cast<uint8_t>(opcode::push_size_0) &&
        (version < static_cast<uint8_t>(opcode::push_positive_1) ||
         version > static_cast<uint8_t>(opcode::push_positive_16))) {
        return false;
    }

    if (bytes[1] != size - witness_program_script_prefix) return false;

    return true;
}

code verify(transaction const& tx, uint32_t input_index, script_flags_t flags, script const& input_script, script const& prevout_script, uint64_t value) {
    using machine::program;
    code ec;

    // SIGPUSHONLY: scriptSig must contain only push operations.
    if (script::is_enabled(flags, machine::script_flags::bch_sigpushonly)) {
        if ( ! script::is_relaxed_push(input_script.operations())) {
            return error::sig_pushonly;
        }
    }

    program input(input_script, tx, input_index, flags, value);
    if ((ec = input.evaluate().error)) {
        return ec;
    }

    program prevout(prevout_script, input);
    if ((ec = prevout.evaluate().error)) {
        return ec;
    }

    if ( ! prevout.stack_result(false)) {
        return error::stack_false;
    }

    bool const is_p2sh_20 = prevout_script.is_pay_to_script_hash(flags);
    bool const is_p2sh_32 = prevout_script.is_pay_to_script_hash_32(flags);
    bool const is_p2sh = is_p2sh_20 || is_p2sh_32;

    uint32_t total_sig_checks = prevout.get_metrics().sig_checks();

    if (is_p2sh) {
        if ( ! script::is_relaxed_push(input_script.operations())) {
            return error::sig_pushonly;
        }

        // Segwit recovery (BCHN): before P2SH evaluation, if the redeem script
        // is a witness program, cleanstack is enabled, scriptSig had exactly one
        // push, and this is P2SH (not P2SH_32), skip the entire P2SH evaluation.
        if (is_p2sh_20 &&
            script::is_enabled(flags, machine::script_flags::bch_cleanstack) &&
            ! script::is_enabled(flags, machine::script_flags::bch_disallow_segwit_recovery) &&
            input_script.operations().size() == 1 &&
            is_witness_program(input.top())) {
            return error::success;
        }

        script embedded_script(input.pop(), false);

        program embedded(embedded_script, std::move(input), true);
        if ((ec = embedded.evaluate().error)) {
            return ec;
        }

        if ( ! embedded.stack_result(false)) {
            return error::stack_false;
        }

        if (script::is_enabled(flags, machine::script_flags::bch_cleanstack) && embedded.size() != 1) {
            return error::cleanstack;
        }

        total_sig_checks = embedded.get_metrics().sig_checks();
    }

    if ( ! is_p2sh && script::is_enabled(flags, machine::script_flags::bch_cleanstack) && prevout.size() != 1) {
        return error::cleanstack;
    }

    // INPUT_SIGCHECKS: density limit on signature checks per input.
    if (script::is_enabled(flags, machine::script_flags::bch_input_sigchecks)) {
        int64_t const sig_size = int64_t(input_script.serialized_size(false));
        int64_t const min_sig_size =
            int64_t(total_sig_checks) * sigchecks_input_density_factor
            - sigchecks_input_density_offset;
        if (sig_size < min_sig_size) {
            return error::invalid_script;
        }
    }

    return error::success;
}

code verify(transaction const& tx, uint32_t input, script_flags_t flags) {
    if (input >= tx.inputs().size()) {
        return error::operation_failed;
    }

    auto const& in = tx.inputs()[input];
    auto const& prevout = in.previous_output().validation.cache;
    return verify(tx, input, flags, in.script(), prevout.script(), prevout.value());
}

} // namespace kth::domain::chain
