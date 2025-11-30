// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/transaction.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/rule_fork.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <expected>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/chain/coin_selection.hpp>

#define FMT_HEADER_ONLY 1
#include <fmt/core.h>

using namespace kth::infrastructure::machine;

namespace kth::domain::chain {

using namespace machine;

// Constructors.
//-----------------------------------------------------------------------------

transaction::transaction()
    : validation{}
{}

transaction::transaction(uint32_t version, uint32_t locktime, input::list const& inputs, output::list const& outputs)
    : transaction_basis(version, locktime, inputs, outputs)
    , validation{}
{}

transaction::transaction(uint32_t version, uint32_t locktime, input::list&& inputs, output::list&& outputs)    
    : transaction_basis(version, locktime, std::move(inputs), std::move(outputs))
    , validation{}
{}

transaction::transaction(transaction const& x, hash_digest const& hash)
    : transaction_basis(x)
    , validation(x.validation)
{
    hash_ = std::make_shared<hash_digest>(hash);
    // validation = x.validation;
}

transaction::transaction(transaction&& x, hash_digest const& hash)
    : transaction_basis(std::move(x))
    , validation(std::move(x.validation))
{
    hash_ = std::make_shared<hash_digest>(hash);
    // validation = std::move(x.validation);
}


transaction::transaction(transaction_basis const& x)
    : transaction_basis(x)
{}

transaction::transaction(transaction_basis&& x) noexcept
    : transaction_basis(std::move(x))
{}

transaction::transaction(transaction const& x)
    : transaction_basis(x)
    , validation(x.validation)
{}

transaction::transaction(transaction&& x) noexcept
    : transaction_basis(std::move(x))
    , validation(std::move(x.validation))
{}

transaction& transaction::operator=(transaction const& x) {
    transaction_basis::operator=(x);
    validation = x.validation;
    return *this;
}

transaction& transaction::operator=(transaction&& x) noexcept {
    transaction_basis::operator=(std::move(static_cast<transaction_basis&&>(x)));
    validation = std::move(x.validation);
    return *this;
}

// protected
void transaction::reset() {
    transaction_basis::reset();
    invalidate_cache();
    outputs_hash_.reset();
    inpoints_hash_.reset();
    sequences_hash_.reset();
    segregated_ = std::nullopt;
    total_input_value_ = std::nullopt;
    total_output_value_ = std::nullopt;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<transaction> transaction::from_data(byte_reader& reader, bool wire) {
    auto basis = transaction_basis::from_data(reader, wire);
    if ( ! basis) {
        return std::unexpected(basis.error());
    }
    return transaction(std::move(*basis));
}

// Serialization.
//-----------------------------------------------------------------------------

// Transactions with empty witnesses always use old serialization (bip144).
// If no inputs are witness programs then witness hash is tx hash (bip141).
data_chunk transaction::to_data(bool wire) const {

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

void transaction::to_data(data_sink& stream, bool wire) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, wire);
}

// Size.
//-----------------------------------------------------------------------------

size_t transaction::serialized_size(bool wire) const {
    // Must be both witness and wire encoding for bip144 serialization.
    return transaction_basis::serialized_size(wire);
}

// Accessors.
//-----------------------------------------------------------------------------

void transaction::set_version(uint32_t value) {
    transaction_basis::set_version(value);
    invalidate_cache();
}

void transaction::set_locktime(uint32_t value) {
    transaction_basis::set_locktime(value);
    invalidate_cache();
}

void transaction::set_inputs(input::list const& value) {
    transaction_basis::set_inputs(value);
    invalidate_cache();
    inpoints_hash_.reset();
    sequences_hash_.reset();
    segregated_ = std::nullopt;
    total_input_value_ = std::nullopt;
}

void transaction::set_inputs(input::list&& value) {
    transaction_basis::set_inputs(std::move(value));
    invalidate_cache();
    segregated_ = std::nullopt;
    total_input_value_ = std::nullopt;
}

void transaction::set_outputs(output::list const& value) {
    transaction_basis::set_outputs(value);
    invalidate_cache();
    outputs_hash_.reset();
    total_output_value_ = std::nullopt;
}

void transaction::set_outputs(output::list&& value) {
    transaction_basis::set_outputs(std::move(value));
    invalidate_cache();
    total_output_value_ = std::nullopt;
}

// Cache.
//-----------------------------------------------------------------------------

// protected
void transaction::invalidate_cache() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    hash_mutex_.lock_upgrade();

    if (hash_) {
        hash_mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        hash_.reset();
        //---------------------------------------------------------------------
        hash_mutex_.unlock_and_lock_upgrade();
    }

    hash_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
#else
    {
        std::shared_lock lock(hash_mutex_);
        if ( ! hash_) {
            return;
        }
    }

    std::unique_lock lock(hash_mutex_);
    hash_.reset();
#endif
}

hash_digest transaction::hash() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    hash_mutex_.lock_upgrade(); //TODO(fernando): use RAII

    if ( ! hash_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        hash_mutex_.unlock_upgrade_and_lock(); //TODO(fernando): use RAII
        hash_ = std::make_shared<hash_digest>(chain::hash(*this));
        hash_mutex_.unlock_and_lock_upgrade();
        //-----------------------------------------------------------------
    }

    auto const hash = *hash_;
    hash_mutex_.unlock_upgrade();
///////////////////////////////////////////////////////////////////////////
    return hash;
#else
    {
        std::shared_lock lock(hash_mutex_);
        if (hash_) {
            return *hash_;
        }
    }

    std::unique_lock lock(hash_mutex_);
    if ( ! hash_) {
        hash_ = std::make_shared<hash_digest>(chain::hash(*this));
    }
    return *hash_;
#endif
}

hash_digest transaction::outputs_hash() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    hash_mutex_.lock_upgrade();

    if ( ! outputs_hash_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        hash_mutex_.unlock_upgrade_and_lock();
        outputs_hash_ = std::make_shared<hash_digest>(to_outputs(*this));
        hash_mutex_.unlock_and_lock_upgrade();
        //-----------------------------------------------------------------
    }

    auto const hash = *outputs_hash_;
    hash_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    return hash;
#else
    {
        std::shared_lock lock(hash_mutex_);
        if (outputs_hash_) {
            return *outputs_hash_;
        }
    }
    std::unique_lock lock(hash_mutex_);
    if ( ! outputs_hash_) {
        outputs_hash_ = std::make_shared<hash_digest>(to_outputs(*this));
    }
    return *outputs_hash_;

#endif
}

hash_digest transaction::inpoints_hash() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    hash_mutex_.lock_upgrade();

    if ( ! inpoints_hash_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        hash_mutex_.unlock_upgrade_and_lock();
        inpoints_hash_ = std::make_shared<hash_digest>(to_inpoints(*this));
        hash_mutex_.unlock_and_lock_upgrade();
        //-----------------------------------------------------------------
    }

    auto const hash = *inpoints_hash_;
    hash_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    return hash;
#else
    {
        std::shared_lock lock(hash_mutex_);
        if (inpoints_hash_) {
            return *inpoints_hash_;
        }
    }
    std::unique_lock lock(hash_mutex_);
    if ( ! inpoints_hash_) {
        inpoints_hash_ = std::make_shared<hash_digest>(to_inpoints(*this));
    }
    return *inpoints_hash_;
#endif
}

hash_digest transaction::sequences_hash() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    hash_mutex_.lock_upgrade();

    if ( ! sequences_hash_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        hash_mutex_.unlock_upgrade_and_lock();
        sequences_hash_ = std::make_shared<hash_digest>(to_sequences(*this));
        hash_mutex_.unlock_and_lock_upgrade();
        //-----------------------------------------------------------------
    }

    auto const hash = *sequences_hash_;
    hash_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    return hash;
#else
    {
        std::shared_lock lock(hash_mutex_);
        if (sequences_hash_) {
            return *sequences_hash_;
        }
    }
    std::unique_lock lock(hash_mutex_);
    if ( ! sequences_hash_) {
        sequences_hash_ = std::make_shared<hash_digest>(to_sequences(*this));
    }
    return *sequences_hash_;
#endif
}

hash_digest transaction::utxos_hash() const {
#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    hash_mutex_.lock_upgrade();

    if ( ! utxos_hash_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        hash_mutex_.unlock_upgrade_and_lock();
        utxos_hash_ = std::make_shared<hash_digest>(to_utxos(*this));
        hash_mutex_.unlock_and_lock_upgrade();
        //-----------------------------------------------------------------
    }

    auto const hash = *utxos_hash_;
    hash_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    return hash;
#else
    {
        std::shared_lock lock(hash_mutex_);
        if (utxos_hash_) {
            return *utxos_hash_;
        }
    }
    std::unique_lock lock(hash_mutex_);
    if ( ! utxos_hash_) {
        utxos_hash_ = std::make_shared<hash_digest>(to_utxos(*this));
    }
    return *utxos_hash_;
#endif
}

// Utilities.
//-----------------------------------------------------------------------------

void transaction::recompute_hash() {
    hash_ = nullptr;
    hash();
}

// Validation helpers.
//-----------------------------------------------------------------------------

// Returns max_uint64 in case of overflow.
uint64_t transaction::total_input_value() const {

#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (total_input_value_ != std::nullopt) {
        auto const value = total_input_value_.value();
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return value;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    auto const value = chain::total_input_value(*this);
    total_input_value_ = value;
    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return value;
#else
    {
        std::shared_lock lock(mutex_);
        if (total_input_value_ != std::nullopt) {
            return total_input_value_.value();
        }
    }
    std::unique_lock lock(mutex_);
    if (total_input_value_ == std::nullopt) {
        total_input_value_ = chain::total_input_value(*this);
    }
    return total_input_value_.value();
#endif
}

// Returns max_uint64 in case of overflow.
uint64_t transaction::total_output_value() const {

#if ! defined(__EMSCRIPTEN__)
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    mutex_.lock_upgrade();

    if (total_output_value_ != std::nullopt) {
        auto const value = total_output_value_.value();
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return value;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    auto const value = chain::total_output_value(*this);
    total_output_value_ = value;
    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return value;
#else
    {
        std::shared_lock lock(mutex_);
        if (total_output_value_ != std::nullopt) {
            return total_output_value_.value();
        }
    }

    std::unique_lock lock(mutex_);
    if (total_output_value_ == std::nullopt) {
        total_output_value_ = chain::total_output_value(*this);
    }
    return total_output_value_.value();
#endif
}

uint64_t transaction::fees() const {
    return floor_subtract(total_input_value(), total_output_value());
}

bool transaction::is_overspent() const {
    return !is_coinbase() && total_output_value() > total_input_value();
}

// Returns max_size_t in case of overflow.
size_t transaction::signature_operations() const {
    auto const state = validation.state;
    auto const bip16 = state ? state->is_enabled(kth::domain::machine::rule_fork::bip16_rule) : true;
    auto const bip141 = false;
    return transaction_basis::signature_operations(bip16, bip141);
}

// size_t transaction::weight() const {
//     // Block weight is 3 * Base size * + 1 * Total size (bip141).
//     return base_size_contribution * serialized_size(true, false) +
//            total_size_contribution * serialized_size(true, true);
// }

// Coinbase transactions return success, to simplify iteration.
code transaction::connect_input(chain_state const& state, size_t input_index) const {
    if (input_index >= inputs().size()) {
        return error::operation_failed;
    }

    if (is_coinbase()) {
        return error::success;
    }

    auto const& prevout = inputs()[input_index].previous_output().validation;

    // Verify that the previous output cache has been populated.
    if ( ! prevout.cache.is_valid()) {
        return error::missing_previous_output;
    }

    auto const forks = state.enabled_forks();
    auto const index32 = static_cast<uint32_t>(input_index);

    // Verify the transaction input script against the previous output.
    // return script::verify(*this, index32, forks);
    return verify(*this, index32, forks);
}

// Validation.
//-----------------------------------------------------------------------------

// These checks are self-contained; blockchain (and so version) independent.
code transaction::check(size_t max_block_size, bool transaction_pool, bool retarget /* = true */) const {
    return transaction_basis::check(total_output_value(), max_block_size, transaction_pool, retarget);
}

code transaction::accept(bool transaction_pool) const {
    auto const state = validation.state;
    return state ? accept(*state, transaction_pool) : error::operation_failed;
}

// These checks assume that prevout caching is completed on all tx.inputs.
code transaction::accept(chain_state const& state, bool transaction_pool) const {
    return transaction_basis::accept(state, is_overspent(), validation.duplicate, transaction_pool);
}

code transaction::connect() const {
    auto const state = validation.state;
    return state ? connect(*state) : error::operation_failed;
}

code transaction::connect(chain_state const& state) const {
    code ec;

    for (size_t input = 0; input < inputs().size(); ++input) {
        if ((ec = connect_input(state, input))) {
            return ec;
        }
    }

    return error::success;
}

code verify(transaction const& tx, uint32_t input_index, uint32_t forks, script const& input_script, script const& prevout_script, uint64_t /*value*/) {
    using machine::program;
    code ec;

    // Evaluate input script.
    program input(input_script, tx, input_index, forks);
    if ((ec = input.evaluate())) {
        return ec;
    }

    // Evaluate output script using stack result from input script.
    program prevout(prevout_script, input);
    if ((ec = prevout.evaluate())) {
        return ec;
    }

    // This precludes bare witness programs of -0 (undocumented).
    if ( ! prevout.stack_result(false)) {
        // TODO(2025-Jul)
        // fmt::print("verify() - embedded.stack_result(false) - 1\n");
        // std::terminate();
        return error::stack_false;
    }

    if (prevout_script.is_pay_to_script_hash(forks) || 
        prevout_script.is_pay_to_script_hash_32(forks)) {
        if ( ! script::is_relaxed_push(input_script.operations())) {
            return error::invalid_script_embed;
        }

        // Embedded script must be at the top of the stack (bip16).
        script embedded_script(input.pop(), false);

        program embedded(embedded_script, std::move(input), true);
        if ((ec = embedded.evaluate())) {
            return ec;
        }

        // This precludes embedded witness programs of -0 (undocumented).
        if ( ! embedded.stack_result(false)) {
            // TODO(2025-Jul)
            // fmt::print("verify() - embedded.stack_result(false) - 2\n");
            // std::terminate();
            return error::stack_false;
        }
    }

    return error::success;
}

code verify(transaction const& tx, uint32_t input, uint32_t forks) {
    if (input >= tx.inputs().size()) {
        return error::operation_failed;
    }

    auto const& in = tx.inputs()[input];
    auto const& prevout = in.previous_output().validation.cache;
    return verify(tx, input, forks, in.script(), prevout.script(), prevout.value());

}


constexpr uint64_t sats_per_byte = 1;
constexpr uint64_t approx_input_size = 148;
constexpr uint64_t approx_output_size = 34;
constexpr uint64_t base_tx_size = 10;

struct utxo_selection {
    uint64_t total_selected_bch; // Total selected in BCH
    size_t utxo_count;           // Total number of selected UTXOs
    uint64_t estimated_size;     // Estimated size of the transaction
    uint64_t amount_to_send;     // Amount to send (original hint)

    // Fungibles: category => total accumulated amount
    boost::unordered_flat_map<hash_digest, uint64_t> fungible_tokens;

    // Non-fungible and Both tokens
    std::vector<token_data_t> non_fungible_and_both_tokens;
};

template <typename Comp>
    requires std::strict_weak_order<Comp, utxo, utxo>
std::expected<utxo_selection, std::error_code> select_utxos_simple(
    std::vector<utxo>& available_utxos,
    uint64_t amount_to_send,
    size_t output_count,
    Comp cmp
) {
    std::sort(available_utxos.begin(), available_utxos.end(), cmp);

    utxo_selection result{};
    result.amount_to_send = amount_to_send;

    for (auto const& u : available_utxos) {
        result.total_selected_bch += u.amount();
        ++result.utxo_count;

        if (u.token_data().has_value()) {
            auto const& token = u.token_data().value();

            if (std::holds_alternative<fungible>(token.data)) {
                result.fungible_tokens[token.id] += uint64_t(std::get<fungible>(token.data).amount);
            } else {
                result.non_fungible_and_both_tokens.emplace_back(token);
            }
        }

        uint64_t const estimated_size = base_tx_size + (result.utxo_count * approx_input_size) + (output_count * approx_output_size);
        uint64_t const estimated_fee = estimated_size * sats_per_byte;
        uint64_t const total_needed = amount_to_send + estimated_fee;
        if (result.total_selected_bch >= total_needed) {
            result.estimated_size = estimated_size;
            return result;
        }
    }
    return std::unexpected(error::insufficient_amount);
}

std::expected<utxo_selection, std::error_code> select_utxos_all(
    std::vector<utxo> const& available_utxos,
    uint64_t amount_to_send,
    size_t output_count
) {
    size_t const utxo_count = available_utxos.size();
    uint64_t const estimated_size = base_tx_size + (utxo_count * approx_input_size) + (output_count * approx_output_size);

    uint64_t const total_input = std::accumulate(available_utxos.begin(), available_utxos.end(),
        uint64_t(0), [](uint64_t sum, utxo const& u) {
            return sum + u.amount();
        });

    uint64_t const estimated_fee = estimated_size * sats_per_byte;
    if (total_input <= estimated_fee) {
        return std::unexpected(error::insufficient_amount);
    }

    amount_to_send = total_input - estimated_fee;

    return utxo_selection {
        total_input,
        utxo_count,
        estimated_size,
        amount_to_send
    };
}

std::vector<double> make_change_ratios(size_t change_count) {
    std::vector<double> change_ratios(change_count, 0.0);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0, 1);
    std::generate(change_ratios.begin(), change_ratios.end(), [&gen, &dis]() { return dis(gen); });
    double const sum = std::accumulate(change_ratios.begin(), change_ratios.end(), 0.0);
    std::transform(change_ratios.begin(), change_ratios.end(), change_ratios.begin(), [sum](double value) { return value / sum; });
    return change_ratios;
}

std::expected<template_result, std::error_code> transaction::create_template(
    std::vector<utxo> available_utxos,
    uint64_t amount_to_send_hint,
    wallet::payment_address const& destination_address,
    std::vector<wallet::payment_address> const& change_addresses,
    std::vector<double> const& change_ratios,
    coin_selection_algorithm selection_algo
) {
    // Validations
    if (available_utxos.empty()) {
        return std::unexpected(error::empty_utxo_list);
    }

    if (change_addresses.empty()) {
        return std::unexpected(error::invalid_change);
    }

    if (change_ratios.size() != change_addresses.size()) {
        return std::unexpected(error::invalid_change);
    }

    if (std::accumulate(change_ratios.begin(), change_ratios.end(), 0.0) != 1.0) {
        return std::unexpected(error::invalid_change);
    }

    bool const send_all = amount_to_send_hint == 0 ||
        selection_algo == coin_selection_algorithm::send_all ||
        selection_algo == coin_selection_algorithm::manual; // in manual mode, we asume that the user provided just the selected utxos

    if (send_all && change_addresses.size() > 1) {
        return std::unexpected(error::invalid_change);
    }

    boost::unordered_flat_map<chain::point, uint32_t> utxo_positions;
    for (size_t i = 0; i < available_utxos.size(); ++i) {
        utxo_positions[available_utxos[i].point()] = i;
    }

    size_t const output_count = change_addresses.size() + 1; // +1 for the destination address

    auto const res =
        send_all ? select_utxos_all(available_utxos, amount_to_send_hint, output_count) :
                   selection_algo == coin_selection_algorithm::smallest_first ?
                       select_utxos_simple(available_utxos, amount_to_send_hint, output_count, [](utxo const& a, utxo const& b) { return a.amount() < b.amount(); }) :
                       select_utxos_simple(available_utxos, amount_to_send_hint, output_count, [](utxo const& a, utxo const& b) { return a.amount() > b.amount(); });

    if ( ! res) {
        return std::unexpected(res.error());
    }

    auto const [
        total_input,
        utxo_count,
        estimated_size,
        amount_to_send,
        fungible_tokens,
        non_fungible_and_both_tokens
    ] = *res;


    std::vector<uint32_t> selected_utxo_indices;
    transaction::ins inputs;

    for (size_t i = 0; i < utxo_count; ++i) {
        auto const ops = script::to_pay_public_key_hash_pattern_unlocking_placeholder(71, 33);
        script placeholder_script;
        placeholder_script.from_operations(ops);

        inputs.emplace_back(available_utxos[i].point(), std::move(placeholder_script), max_input_sequence);
        auto const it = utxo_positions.find(available_utxos[i].point());
        if (it != utxo_positions.end()) {
            selected_utxo_indices.push_back(it->second);
        }
    }

    std::vector<wallet::payment_address> output_addresses;
    std::vector<uint64_t> output_amounts;

    transaction::outs outputs;
    auto dest_ops = script::to_pay_public_key_hash_pattern(destination_address.hash20());
    chain::script dest_script;
    dest_script.from_operations(dest_ops);
    outputs.emplace_back(amount_to_send, std::move(dest_script), std::nullopt);

    output_addresses.push_back(destination_address);
    output_amounts.push_back(amount_to_send);

    uint64_t const estimated_fee = estimated_size * sats_per_byte;

    if ( ! send_all) {
        uint64_t const total_change_amount = total_input - (amount_to_send + estimated_fee);
        uint64_t accumulated_change = 0;

        if (total_change_amount > 0) {
            for (size_t i = 0; i < change_addresses.size(); ++i) {
                auto change_ops = script::to_pay_public_key_hash_pattern(change_addresses[i].hash20());
                chain::script change_script;
                change_script.from_operations(change_ops);
                uint64_t const change_amount = total_change_amount * change_ratios[i];
                outputs.emplace_back(change_amount, std::move(change_script), std::nullopt);
                output_addresses.push_back(change_addresses[i]);
                output_amounts.push_back(change_amount);
                accumulated_change += change_amount;
            }
        }
        if (accumulated_change != total_change_amount) {
            return std::unexpected(error::unknown);
        }
        uint64_t const total_output = total_change_amount + amount_to_send + estimated_fee;
        if (total_input != total_output) {
            return std::unexpected(error::unknown);
        }
        // postcondition validation
    } else {
        // postcondition validation
        uint64_t const total_output = amount_to_send + estimated_fee;
        if (total_input != total_output) {
            return std::unexpected(error::unknown);
        }
    }

    auto const& change_address = change_addresses[0];
    for (auto const& token_pair : fungible_tokens) {

        token_data_t token {
            .id = token_pair.first,
            .data = fungible {
                .amount = amount_t{int64_t(token_pair.second)}
            }
        };

        auto change_ops = script::to_pay_public_key_hash_pattern(change_address.hash20());
        chain::script change_script;
        change_script.from_operations(change_ops);
        uint64_t const dust_bch_amount = 0;
        outputs.emplace_back(dust_bch_amount, std::move(change_script), token);
    }

    for (auto const& token : non_fungible_and_both_tokens) {
        auto change_ops = script::to_pay_public_key_hash_pattern(change_address.hash20());
        chain::script change_script;
        change_script.from_operations(change_ops);
        uint64_t const dust_bch_amount = 0;
        outputs.emplace_back(dust_bch_amount, std::move(change_script), token);
    }

    return std::make_tuple(
        transaction(1, 0, std::move(inputs), std::move(outputs)),
        std::move(selected_utxo_indices),
        std::move(output_addresses),
        std::move(output_amounts)
    );
}

std::expected<template_result, std::error_code> transaction::create_template(
    std::vector<utxo> available_utxos,
    uint64_t amount_to_send_hint,
    wallet::payment_address const& destination_address,
    std::vector<wallet::payment_address> const& change_addresses,
    coin_selection_algorithm selection_algo
) {
    return create_template(
        available_utxos,
        amount_to_send_hint,
        destination_address,
        change_addresses,
        make_change_ratios(change_addresses.size()),
        selection_algo);
}

} // namespace kth::domain::chain
