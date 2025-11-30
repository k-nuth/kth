// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/transaction_basis.hpp>

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
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/rule_fork.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

using namespace kth::infrastructure::machine;

namespace kth::domain::chain {

// Constructors.
//-----------------------------------------------------------------------------

transaction_basis::transaction_basis(uint32_t version, uint32_t locktime, input::list const& inputs, output::list const& outputs)
    : version_(version)
    , locktime_(locktime)
    , inputs_(inputs)
    , outputs_(outputs)
{}

transaction_basis::transaction_basis(uint32_t version, uint32_t locktime, input::list&& inputs, output::list&& outputs)
    : version_(version)
    , locktime_(locktime)
    , inputs_(std::move(inputs))
    , outputs_(std::move(outputs))
{}

// Operators.
//-----------------------------------------------------------------------------

bool transaction_basis::operator==(transaction_basis const& x) const {
    return (version_ == x.version_) && (locktime_ == x.locktime_) && (inputs_ == x.inputs_) && (outputs_ == x.outputs_);
}

bool transaction_basis::operator!=(transaction_basis const& x) const {
    return !(*this == x);
}

// protected
void transaction_basis::reset() {
    version_ = 0;
    locktime_ = 0;
    inputs_.clear();
    inputs_.shrink_to_fit();
    outputs_.clear();
    outputs_.shrink_to_fit();
}

bool transaction_basis::is_valid() const {
    return (version_ != 0) || (locktime_ != 0) || !inputs_.empty() || !outputs_.empty();
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<transaction_basis> transaction_basis::from_data(byte_reader& reader, bool wire /*= true*/) {
    if (wire) {
        // Wire (satoshi protocol) deserialization.
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
        return transaction_basis {
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

    return transaction_basis {
        uint32_t(*version),
        uint32_t(*locktime),
        std::move(*inputs),
        std::move(*outputs)
    };
}


// Serialization.
//-----------------------------------------------------------------------------

// Transactions with empty witnesses always use old serialization (bip144).
// If no inputs are witness programs then witness hash is tx hash (bip141).
data_chunk transaction_basis::to_data(bool wire) const {
    data_chunk data;
    auto const size = serialized_size(wire);

    // Reserve an extra byte to prevent full reallocation in the case of
    // generate_signature_hash extension by addition of the sighash_type.
    data.reserve(size + sizeof(uint8_t));

    data_sink ostream(data);
    to_data(ostream, wire);

    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void transaction_basis::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Size.
//-----------------------------------------------------------------------------

size_t transaction_basis::serialized_size(bool wire) const {
    // Returns space for the witness although not serialized by input.
    // Returns witness space if specified even if input not segregated.
    auto const ins = [wire](size_t size, input const& input) {
        return size + input.serialized_size(wire);
    };

    auto const outs = [wire](size_t size, output const& output) {
        return size + output.serialized_size(wire);
    };

    // Must be both witness and wire encoding for bip144 serialization.
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

uint32_t transaction_basis::version() const {
    return version_;
}

void transaction_basis::set_version(uint32_t value) {
    version_ = value;
}

uint32_t transaction_basis::locktime() const {
    return locktime_;
}

void transaction_basis::set_locktime(uint32_t value) {
    locktime_ = value;
}

input::list& transaction_basis::inputs() {
    return inputs_;
}

input::list const& transaction_basis::inputs() const {
    return inputs_;
}

void transaction_basis::set_inputs(input::list const& value) {
    inputs_ = value;
}

void transaction_basis::set_inputs(input::list&& value) {
    inputs_ = std::move(value);
}

output::list& transaction_basis::outputs() {
    return outputs_;
}

output::list const& transaction_basis::outputs() const {
    return outputs_;
}

void transaction_basis::set_outputs(output::list const& value) {
    outputs_ = value;
}

void transaction_basis::set_outputs(output::list&& value) {
    outputs_ = std::move(value);
}


// Validation helpers.
//-----------------------------------------------------------------------------

bool transaction_basis::is_coinbase() const {
    return inputs_.size() == 1 && inputs_.front().previous_output().is_null();
}

// True if coinbase and has invalid input[0] script size.
bool transaction_basis::is_oversized_coinbase() const {
    if ( ! is_coinbase()) {
        return false;
    }

    auto const script_size = inputs_.front().script().serialized_size(false);
    return script_size < min_coinbase_size || script_size > max_coinbase_size;
}

// True if not coinbase but has null previous_output(s).
bool transaction_basis::is_null_non_coinbase() const {
    if (is_coinbase()) {
        return false;
    }

    auto const invalid = [](input const& input) {
        return input.previous_output().is_null();
    };

    return std::any_of(inputs_.begin(), inputs_.end(), invalid);
}

// private
bool transaction_basis::all_inputs_final() const {
    auto const finalized = [](input const& input) {
        return input.is_final();
    };

    return std::all_of(inputs_.begin(), inputs_.end(), finalized);
}

bool transaction_basis::is_final(size_t block_height, uint32_t block_time) const {
    auto const max_locktime = [=, this]() {
        return locktime_ < locktime_threshold ? *safe_unsigned<uint32_t>(block_height) : block_time;
    };

    return locktime_ == 0 || locktime_ < max_locktime() || all_inputs_final();
}

bool transaction_basis::is_locked(size_t block_height,
                            uint32_t median_time_past) const {
    if (version_ < relative_locktime_min_version || is_coinbase()) {
        return false;
    }

    auto const locked = [block_height, median_time_past](input const& input) {
        return input.is_locked(block_height, median_time_past);
    };

    // If any input is relative time locked the transaction is as well.
    return std::any_of(inputs_.begin(), inputs_.end(), locked);
}

// This is not a consensus rule, just detection of an irrational use.
bool transaction_basis::is_locktime_conflict() const {
    return locktime_ != 0 && all_inputs_final();
}

// Returns max_size_t in case of overflow.
size_t transaction_basis::signature_operations(bool bip16, bool bip141) const {
    bip141 = false;  // No segwit
    auto const in = [bip16, bip141](size_t total, input const& input) {
        // This includes BIP16 p2sh additional sigops if prevout is cached.
        return ceiling_add(total, input.signature_operations(bip16, bip141));
    };

    auto const out = [bip141](size_t total, output const& output) {
        return ceiling_add(total, output.signature_operations(bip141));
    };

    return std::accumulate(inputs_.begin(), inputs_.end(), size_t{0}, in) +
           std::accumulate(outputs_.begin(), outputs_.end(), size_t{0}, out);
}

bool transaction_basis::is_missing_previous_outputs() const {
    auto const missing = [](input const& input) {
        auto const& prevout = input.previous_output();
        auto const coinbase = prevout.is_null();
        auto const missing = !prevout.validation.cache.is_valid();
        return missing && !coinbase;
    };

    // This is an optimization of !missing_inputs().empty();
    return std::any_of(inputs_.begin(), inputs_.end(), missing);
}

point::list transaction_basis::previous_outputs() const {
    point::list prevouts;
    prevouts.reserve(inputs_.size());
    auto const pointer = [&prevouts](input const& input) {
        prevouts.push_back(input.previous_output());
    };

    auto const& ins = inputs_;
    std::for_each(ins.begin(), ins.end(), pointer);
    return prevouts;
}

point::list transaction_basis::missing_previous_outputs() const {
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

hash_list transaction_basis::missing_previous_transactions() const {
    auto const points = missing_previous_outputs();
    hash_list hashes;
    hashes.reserve(points.size());
    auto const hasher = [&hashes](output_point const& point) {
        return point.hash();
    };

    std::for_each(points.begin(), points.end(), hasher);
    return distinct(hashes);
}

bool transaction_basis::is_internal_double_spend() const {
    auto prevouts = previous_outputs();
    std::sort(prevouts.begin(), prevouts.end());
    auto const distinct_end = std::unique(prevouts.begin(), prevouts.end());
    auto const distinct = (distinct_end == prevouts.end());
    return !distinct;
}

bool transaction_basis::is_double_spend(bool include_unconfirmed) const {
    auto const spent = [include_unconfirmed](input const& input) {
        auto const& prevout = input.previous_output().validation;
        return prevout.spent && (include_unconfirmed || prevout.confirmed);
    };

    return std::any_of(inputs_.begin(), inputs_.end(), spent);
}

bool transaction_basis::is_dusty(uint64_t minimum_output_value) const {
    auto const dust = [minimum_output_value](output const& output) {
        return output.is_dust(minimum_output_value);
    };

    return std::any_of(outputs_.begin(), outputs_.end(), dust);
}

bool transaction_basis::is_mature(size_t height) const {
    auto const mature = [height](input const& input) {
        return input.previous_output().is_mature(height);
    };

    return std::all_of(inputs_.begin(), inputs_.end(), mature);
}

// Validation.
//-----------------------------------------------------------------------------

// These checks are self-contained; blockchain (and so version) independent.
code transaction_basis::check(uint64_t total_output_value, size_t max_block_size, bool transaction_pool, bool retarget) const {
    if (inputs_.empty() || outputs_.empty()) {
        return error::empty_transaction;
    }

    if (is_null_non_coinbase()) {
        return error::previous_output_null;
    }

    // if (total_output_value() > max_money(retarget)) {
    if (total_output_value > max_money(retarget)) {
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
        // TODO(legacy): reduce by header, txcount and smallest coinbase size for height.
    }

    if (transaction_pool && serialized_size(true) >= max_block_size) {
        return error::transaction_size_limit;

        // We cannot know if bip16/bip141 is enabled here so we do not check it.
        // This will not make a difference unless prevouts are populated, in which
        // case they are ignored. This means that p2sh sigops are not counted here.
        // This is a preliminary check, the final count must come from accept().
        // Reenable once sigop caching is implemented, otherwise is deoptimization.
        ////else if (transaction_pool && signature_operations(false, false) > get_max_block_sigops()
        ////    return error::transaction_legacy_sigop_limit;
    }

    return error::success;
}

size_t transaction_basis::min_tx_size(chain_state const& state) const {
#if defined(KTH_CURRENCY_BCH)
    if (state.is_descartes_enabled()) {
        return min_transaction_size_descartes;      // 2023-May-15
    }
    if (state.is_euclid_enabled()) {
        return min_transaction_size_old;            // 2018-Nov-15
    }
#endif
    return 0;
}

// These checks assume that prevout caching is completed on all tx.inputs.
code transaction_basis::accept(chain_state const& state, bool is_overspent, bool is_duplicated /*= false*/, bool transaction_pool /*= true*/) const {
    auto const bip16 = state.is_enabled(kth::domain::machine::rule_fork::bip16_rule);
    auto const bip30 = state.is_enabled(kth::domain::machine::rule_fork::bip30_rule);
    auto const bip68 = state.is_enabled(kth::domain::machine::rule_fork::bip68_rule);
    auto const network = state.network();
    auto const bip141 = false;  // No segwit

    auto const revert_bip30 = state.is_enabled(kth::domain::machine::rule_fork::allow_collisions);

    if (transaction_pool && state.is_under_checkpoint()) {
        return error::premature_validation;
    }

#if defined(KTH_CURRENCY_BCH)
    if (serialized_size(true) < min_tx_size(state)) {
        return error::transaction_size_limit;
    }
#endif

    if (transaction_pool && !is_final(state.height(), state.median_time_past())) {
        return error::transaction_non_final;
    }

    //*************************************************************************
    // CONSENSUS:
    // A transaction hash that exists in the chain is not acceptable even if
    // the original is spent in the new block. This is not necessary nor is it
    // described by BIP30, but it is in the code referenced by BIP30.
    //*************************************************************************
    if (bip30 && !revert_bip30 && is_duplicated) {
        return error::unspent_duplicate;
    }

    if (is_missing_previous_outputs()) {
        return error::missing_previous_output;
    }

    if (is_double_spend(transaction_pool)) {
        return error::double_spend;
        // This relates height to maturity of spent coinbase. Since reorg is the
        // only way to decrease height and reorg invalidates, this is cache safe.
    }

    if ( ! is_mature(state.height())) {
        return error::coinbase_maturity;
    }

    if (is_overspent) {
        return error::spend_exceeds_value;
    }

    if (bip68 && is_locked(state.height(), state.median_time_past())) {
        return error::sequence_locked;
        // This recomputes sigops to include p2sh from prevouts if bip16 is true.
    }

#if defined(KTH_CURRENCY_BCH)
    if ( ! state.is_fermat_enabled()) {
#endif
        auto const max_sigops = state.is_lobachevski_enabled() ?
            state.dynamic_max_block_sigops() :
            static_max_block_sigops(network);


        if (transaction_pool && signature_operations(bip16, bip141) > max_sigops) {
            return error::transaction_embedded_sigop_limit;
            // This causes second serialized_size(true, false) computation (uncached).
            // TODO(legacy): reduce by header, txcount and smallest coinbase size for height.
        }
#if defined(KTH_CURRENCY_BCH)
    }

    if (state.is_descartes_enabled()) {
        // CHIP 2021-01 Restrict Transaction Version. Enabled in 2023-May-15
        if (version_ > transaction_version_max || version_ < transaction_version_min) {
            return error::transaction_version_out_of_range;
        }
    }
#endif

    return error::success;
}

bool transaction_basis::is_standard() const {
    for (auto const& in : inputs()) {
        if (in.script().pattern() == script_pattern::non_standard) {
            return false;
        }
    }

    return std::all_of(begin(outputs()), end(outputs()), [](auto const& out){
        return out.script().pattern() != script_pattern::non_standard;
    });
}

hash_digest hash(transaction_basis const& tx) {
    return bitcoin_hash(tx.to_data(true));
}

hash_digest outputs_hash(transaction_basis const& tx) {
    return to_outputs(tx);
}

hash_digest inpoints_hash(transaction_basis const& tx) {
    return to_inpoints(tx);
}

hash_digest sequences_hash(transaction_basis const& tx) {
    return to_sequences(tx);
}

hash_digest utxos_hash(transaction_basis const& tx) {
    return to_utxos(tx);
}

hash_digest to_outputs(transaction_basis const& tx) {
    auto const sum = [&](size_t total, output const& output) {
        return total + output.serialized_size();
    };

    auto const& outs = tx.outputs();
    auto size = std::accumulate(outs.begin(), outs.end(), size_t(0), sum);
    data_chunk data;
    data.reserve(size);
    data_sink ostream(data);
    ostream_writer sink_w(ostream);

    auto const write = [&](output const& output) {
        output.to_data(sink_w, true);
    };

    std::for_each(outs.begin(), outs.end(), write);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return bitcoin_hash(data);
}

hash_digest to_inpoints(transaction_basis const& tx) {
    auto const sum = [&](size_t total, input const& input) {
        return total + input.previous_output().serialized_size();
    };

    auto const& ins = tx.inputs();
    auto size = std::accumulate(ins.begin(), ins.end(), size_t(0), sum);
    data_chunk data;
    data.reserve(size);
    data_sink ostream(data);
    ostream_writer sink_w(ostream);

    auto const write = [&](input const& input) {
        input.previous_output().to_data(sink_w);
    };

    std::for_each(ins.begin(), ins.end(), write);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return bitcoin_hash(data);
}

hash_digest to_sequences(transaction_basis const& tx) {
    auto const sum = [&](size_t total, input const& /*input*/) {
        return total + sizeof(uint32_t);
    };

    auto const& ins = tx.inputs();
    auto size = std::accumulate(ins.begin(), ins.end(), size_t(0), sum);
    data_chunk data;
    data.reserve(size);
    data_sink ostream(data);
    ostream_writer sink_w(ostream);

    auto const write = [&](input const& input) {
        sink_w.write_4_bytes_little_endian(input.sequence());
    };

    std::for_each(ins.begin(), ins.end(), write);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return bitcoin_hash(data);
}

hash_digest to_utxos(transaction_basis const& tx) {
    auto const sum = [&](size_t total, input const& input) {
        auto const& prevout = input.previous_output().validation.cache;
        auto const missing = !prevout.is_valid();
        total += missing ? 0 : prevout.serialized_size();
        return total;
    };

    auto const& ins = tx.inputs();
    auto const size = std::accumulate(ins.begin(), ins.end(), size_t(0), sum);

    data_chunk data;
    data.reserve(size);
    data_sink ostream(data);
    ostream_writer sink_w(ostream);

    auto const write = [&](input const& input) {
        auto const& prevout = input.previous_output().validation.cache;
        auto const missing = !prevout.is_valid();
        if (missing) return;
        prevout.to_data(sink_w);
    };

    std::for_each(ins.begin(), ins.end(), write);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return bitcoin_hash(data);
}

// Returns max_uint64 in case of overflow.
uint64_t total_input_value(transaction_basis const& tx) {
    ////static_assert(max_money() < max_uint64, "overflow sentinel invalid");
    auto const sum = [](uint64_t total, input const& input) {
        auto const& prevout = input.previous_output().validation.cache;
        auto const missing = !prevout.is_valid();

        // Treat missing previous outputs as zero-valued, no math on sentinel.
        return ceiling_add(total, missing ? 0 : prevout.value());
    };

    return std::accumulate(tx.inputs().begin(), tx.inputs().end(), uint64_t(0), sum);
}

// Returns max_uint64 in case of overflow.
uint64_t total_output_value(transaction_basis const& tx) {
    ////static_assert(max_money() < max_uint64, "overflow sentinel invalid");
    auto const sum = [](uint64_t total, output const& output) {
        return ceiling_add(total, output.value());
    };

    return std::accumulate(tx.outputs().begin(), tx.outputs().end(), uint64_t(0), sum);
}

uint64_t fees(transaction_basis const& tx) {
    return floor_subtract(total_input_value(tx), total_output_value(tx));
}

bool is_overspent(transaction_basis const& tx) {
    return ! tx.is_coinbase() && total_output_value(tx) > total_input_value(tx);
}

} // namespace kth::domain::chain
