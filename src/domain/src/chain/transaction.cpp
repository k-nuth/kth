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

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <expected>

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
    auto const bip16 = state ? state->is_enabled(kth::domain::machine::script_flags::bip16_rule) : true;
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

    auto const flags = state.enabled_flags();
    auto const index32 = uint32_t(input_index);

    // Verify the transaction input script against the previous output.
    // return script::verify(*this, index32, flags);
    return verify(*this, index32, flags);
}

// Validation.
//-----------------------------------------------------------------------------

code transaction::check(size_t max_block_size, bool transaction_pool, bool retarget) const {
    return transaction_basis::check(total_output_value(), max_block_size, transaction_pool, retarget);
}

code transaction::accept(
    script_flags_t flags,
    size_t height,
    uint32_t median_time_past,
    size_t max_sigops,
    bool is_under_checkpoint,
    bool transaction_pool
) const {
    return transaction_basis::accept(
        flags, height, median_time_past, max_sigops,
        is_overspent(), validation.duplicate,
        is_under_checkpoint, transaction_pool
    );
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

// A witness program is: version_byte (OP_0 or OP_1..OP_16) + push_byte + program_data.
// Total script size must be min_witness_program_script..max_witness_program_script bytes.
// This is used for segwit recovery: P2SH redeem scripts that look like witness
// programs are exempt from the cleanstack rule (BCHN: IsWitnessProgram).
bool is_witness_program(data_chunk const& bytes) {
    using machine::opcode;
    auto const size = bytes.size();
    if (size < min_witness_program_script || size > max_witness_program_script) return false;

    // Version byte: OP_0 (push_size_0) or OP_1..OP_16 (push_positive_1..push_positive_16)
    auto const version = bytes[0];
    if (version != static_cast<uint8_t>(opcode::push_size_0) &&
        (version < static_cast<uint8_t>(opcode::push_positive_1) ||
         version > static_cast<uint8_t>(opcode::push_positive_16))) {
        return false;
    }

    // Push byte must cover exactly the remaining bytes
    if (bytes[1] != size - witness_program_script_prefix) return false;

    return true;
}

code verify(transaction const& tx, uint32_t input_index, script_flags_t flags, script const& input_script, script const& prevout_script, uint64_t value) {
    using machine::program;
    code ec;

    // SIGPUSHONLY: scriptSig must contain only push operations.
    // BCHN checks this before any evaluation.
    if (script::is_enabled(flags, machine::script_flags::bch_sigpushonly)) {
        if ( ! script::is_relaxed_push(input_script.operations())) {
            return error::sig_pushonly;
        }
    }

    // Evaluate input script.
    program input(input_script, tx, input_index, flags, value);
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

    bool const is_p2sh_20 = prevout_script.is_pay_to_script_hash(flags);
    bool const is_p2sh_32 = prevout_script.is_pay_to_script_hash_32(flags);
    bool const is_p2sh = is_p2sh_20 || is_p2sh_32;

    // Track cumulative sig_checks across all evaluations.
    // Metrics propagate through program constructors (matching BCHN's single metrics object).
    uint32_t total_sig_checks = prevout.get_metrics().sig_checks();

    if (is_p2sh) {
        if ( ! script::is_relaxed_push(input_script.operations())) {
            return error::sig_pushonly;
        }

        // Segwit recovery (BCHN): before P2SH evaluation, if the redeem script
        // is a witness program, cleanstack is enabled, scriptSig had exactly one
        // push, and this is P2SH (not P2SH_32), skip the entire P2SH evaluation.
        // This allows recovery of coins accidentally sent to SegWit addresses on BCH.
        if (is_p2sh_20 &&
            script::is_enabled(flags, machine::script_flags::bch_cleanstack) &&
            ! script::is_enabled(flags, machine::script_flags::bch_disallow_segwit_recovery) &&
            input_script.operations().size() == 1 &&
            is_witness_program(input.top())) {
            return error::success;
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

        // Cleanstack: after P2SH execution, stack must have exactly one element.
        if (script::is_enabled(flags, machine::script_flags::bch_cleanstack) && embedded.size() != 1) {
            return error::cleanstack;
        }

        total_sig_checks = embedded.get_metrics().sig_checks();
    }

    // Cleanstack (non-P2SH): after execution, stack must have exactly one element.
    if ( ! is_p2sh && script::is_enabled(flags, machine::script_flags::bch_cleanstack) && prevout.size() != 1) {
        return error::cleanstack;
    }

    // INPUT_SIGCHECKS: density limit on signature checks per input.
    // Formula from BCHN: scriptSig.size() >= sigChecks * 43 - 60
    if (script::is_enabled(flags, machine::script_flags::bch_input_sigchecks)) {
        auto const sig_size = int(input_script.serialized_size(false));
        if (sig_size < int(total_sig_checks) * sigchecks_input_density_factor - sigchecks_input_density_offset) {
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
