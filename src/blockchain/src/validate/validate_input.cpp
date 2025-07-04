// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_input.hpp>

#include <cstdint>

#include <kth/domain.hpp>

#ifdef WITH_CONSENSUS
#include <kth/consensus.hpp>
#endif

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::machine;

#ifdef WITH_CONSENSUS

using namespace kth::consensus;

//TODO(legacy): map bc policy flags.
uint32_t validate_input::convert_flags(uint32_t forks) {
    uint32_t flags = verify_flags_none;

    if (script::is_enabled(forks, domain::machine::rule_fork::bip16_rule)) {
        flags |= verify_flags_p2sh;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bip65_rule)) {
        flags |= verify_flags_checklocktimeverify;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bip66_rule)) {
        flags |= verify_flags_dersig;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bip112_rule)) {
        flags |= verify_flags_checksequenceverify;
    }

#if defined(KTH_CURRENCY_BCH)

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_uahf)) {
        flags |= verify_flags_strictenc;
        flags |= verify_flags_enable_sighash_forkid;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_daa_cw144)) {
        flags |= verify_flags_low_s;
        flags |= verify_flags_null_fail;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_euclid)) {
        flags |= verify_flags_sigpushonly;
        flags |= verify_flags_cleanstack;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_mersenne)) {
        flags |= verify_flags_enable_schnorr_multisig;
        flags |= verify_flags_minimaldata;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_fermat)) {
        flags |= verify_flags_enforce_sigchecks;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_gauss)) {
        flags |= verify_flags_64_bit_integers;
        flags |= verify_flags_native_introspection;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_descartes)) {
        flags |= verify_flags_enable_p2sh_32;
        flags |= verify_flags_enable_tokens;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bch_galois)) {
        flags |= verify_flags_enable_may2025;
        // Note: this flag is used just for mempool
        // flags |= verify_flags_enable_vm_limits_standard;
    }

    // // We make sure this node will have replay protection during the next hard fork.
    // if (script::is_enabled(forks, domain::machine::rule_fork::bch_replay_protection)) {
    //     flags |= verify_flags_enable_replay_protection;
    // }

#else
    if (script::is_enabled(forks, domain::machine::rule_fork::bip141_rule)) {
        flags |= verify_flags_witness;
    }

    if (script::is_enabled(forks, domain::machine::rule_fork::bip147_rule)) {
        flags |= verify_flags_nulldummy;
    }
#endif
    return flags;
}

// TODO: map to corresponding kd::error codes.
code validate_input::convert_result(verify_result_type result) {
    switch (result) {
        // Logical true result.
        case verify_result_type::verify_result_eval_true:
            return error::success;

        // Logical false result.
        case verify_result_type::verify_result_eval_false:
            return error::stack_false;

        // Max size errors.
        case verify_result_type::verify_result_script_size:
        case verify_result_type::verify_result_push_size:
        case verify_result_type::verify_result_op_count:
        case verify_result_type::verify_result_stack_size:
        case verify_result_type::verify_result_sig_count:
        case verify_result_type::verify_result_pubkey_count:
            return error::invalid_script;

        // Failed verify operations.
        case verify_result_type::verify_result_verify:
        case verify_result_type::verify_result_equalverify:
        case verify_result_type::verify_result_checkmultisigverify:
        case verify_result_type::verify_result_checksigverify:
        case verify_result_type::verify_result_numequalverify:
            return error::invalid_script;

        // Logical/Format/Canonical errors.
        case verify_result_type::verify_result_bad_opcode:
        case verify_result_type::verify_result_disabled_opcode:
        case verify_result_type::verify_result_invalid_stack_operation:
        case verify_result_type::verify_result_invalid_altstack_operation:
        case verify_result_type::verify_result_unbalanced_conditional:
            return error::invalid_script;

        // Softfork safeness (should not see).
        case verify_result_type::verify_result_discourage_upgradable_nops:
            return error::operation_failed;
#if ! defined(KTH_CURRENCY_BCH)
        case verify_result_type::verify_result_discourage_upgradable_witness_program:
            return error::operation_failed;

        // BIP66 errors (also BIP62, which is undeployed).
        case verify_result_type::verify_result_sig_der:
            return error::invalid_signature_encoding;
#endif
        // BIP62 signature validation errors
        case verify_result_type::verify_result_sig_hashtype:
            return error::sig_hashtype;
        case verify_result_type::verify_result_minimaldata:
            return error::minimaldata;
        case verify_result_type::verify_result_sig_pushonly:
            return error::sig_pushonly;
        case verify_result_type::verify_result_sig_high_s:
            return error::sig_high_s;

#if ! defined(KTH_CURRENCY_BCH)
        case verify_result_type::verify_result_sig_nulldummy:
            return error::sig_nullfail;
#endif

        case verify_result_type::verify_result_pubkeytype:
            return error::pubkey_type;
        case verify_result_type::verify_result_cleanstack:
            return error::cleanstack;

        // BIP65/BIP112 (shared codes).
        case verify_result_type::verify_result_negative_locktime:
            return error::negative_locktime;
        case verify_result_type::verify_result_unsatisfied_locktime:
            return error::unsatisfied_locktime;

        // Other errors.
        case verify_result_type::verify_result_op_return:
        case verify_result_type::verify_result_unknown_error:
            return error::invalid_script;

#if ! defined(KTH_CURRENCY_BCH)
        // Segregated witness.
        case verify_result_type::verify_result_witness_program_wrong_length:
        case verify_result_type::verify_result_witness_program_empty_witness:
        case verify_result_type::verify_result_witness_program_mismatch:
        case verify_result_type::verify_result_witness_malleated:
        case verify_result_type::verify_result_witness_malleated_p2sh:
        case verify_result_type::verify_result_witness_unexpected:
        case verify_result_type::verify_result_witness_pubkeytype:
            return error::invalid_script;
#endif

        // Augmention codes for tx deserialization.
        case verify_result_type::verify_result_tx_invalid:
        case verify_result_type::verify_result_tx_size_invalid:
        case verify_result_type::verify_result_tx_input_invalid:
            return error::invalid_script;

        default:
            return error::invalid_script;
    }
}

using coins_t = std::vector<std::vector<uint8_t>>;

inline
coins_t create_context_data(transaction const& tx, bool should_create_context) {
    if ( ! should_create_context) return {};

    coins_t coins;
    coins.reserve(tx.inputs().size());

    for (auto const& input : tx.inputs()) {
        auto const& prevout = input.previous_output().validation.cache;
        coins.emplace_back(prevout.to_data(true));
    }
    return coins;
}

std::pair<code, size_t> validate_input::verify_script(transaction const& tx, uint32_t input_index, uint32_t forks) {
    constexpr bool prefix = false;

    KTH_ASSERT(input_index < tx.inputs().size());
    auto const& prevout = tx.inputs()[input_index].previous_output().validation;
    auto const locking_script_data = prevout.cache.script().to_data(false);
    auto const token_data = prevout.cache.token_data();
    auto const amount = prevout.cache.value();
    auto const tx_data = tx.to_data(true);

    size_t sig_checks;
    bool const should_create_context = script::is_enabled(forks, domain::machine::rule_fork::bch_gauss);
    auto const coins = create_context_data(tx, should_create_context);
    auto const unlock_script_data = tx.inputs()[input_index].script().to_data(prefix);

    auto res = consensus::verify_script(
        tx_data.data(),
        tx_data.size(),
        locking_script_data.data(),
        locking_script_data.size(),
        unlock_script_data.data(),
        unlock_script_data.size(),
        input_index,
        convert_flags(forks),
        sig_checks,
        amount,
        coins
    );

    return {convert_result(res), sig_checks};
}

#else //WITH_CONSENSUS

// #error Not supported, build using -o consensus=True

std::pair<code, size_t> validate_input::verify_script(transaction const& tx, uint32_t input_index, uint32_t forks) {
    return {script::verify(tx, input_index, forks), 0};
}

#endif //WITH_CONSENSUS

} // namespace kth::blockchain
