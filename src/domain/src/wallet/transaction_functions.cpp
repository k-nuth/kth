// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//TODO(fernando): refactor everything

#include <kth/domain/wallet/transaction_functions.hpp>

#include <kth/domain/config/input.hpp>
#include <kth/domain/config/output.hpp>
#include <kth/domain/config/script.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>

namespace kth::domain::wallet {

//https://github.com/libbitcoin/libbitcoin-explorer/blob/master/src/commands/tx-encode.cpp
static
bool push_scripts(chain::output::list& outputs, config::output const& output, uint8_t script_version) {
    static constexpr uint64_t no_amount = 0;

    // explicit script
    if ( ! output.is_stealth() && output.script().is_valid()) {
        outputs.push_back({output.amount(), output.script(), chain::token_data_opt{}});
        return true;
    }

    // If it's not explicit the script must be a form of pay to short hash.
    if (output.pay_to_hash() == kth::null_short_hash) {
        return false;
    }

    machine::operation::list payment_ops;
    auto const hash = output.pay_to_hash();

    // This presumes stealth versions are the same as non-stealth.
    if (output.version() != script_version) {
        payment_ops = chain::script::to_pay_public_key_hash_pattern(hash);
    } else if (output.version() == script_version) {
        payment_ops = chain::script::to_pay_script_hash_pattern(hash);
    } else {
        return false;
    }

    // If stealth add null data stealth output immediately before payment.
    if (output.is_stealth()) {
        // outputs.push_back({no_amount, output.script(), chain::token_data_opt{}});
        outputs.emplace_back(no_amount, output.script(), chain::token_data_opt{});
    }

    // outputs.push_back({output.amount(), {payment_ops}, chain::token_data_opt{}});
    outputs.emplace_back(output.amount(), chain::script{payment_ops}, chain::token_data_opt{});
    return true;
}

//https://github.com/libbitcoin/libbitcoin-explorer/blob/master/src/commands/tx-encode.cpp
std::pair<error::error_code_t, chain::transaction> tx_encode(chain::input_point::list const& outputs_to_spend,
                                                             raw_output_list const& destiny_and_amount,
                                                             chain::output::list const& extra_outputs /*= {}*/,
                                                             uint32_t locktime /*= 0*/,
                                                             uint32_t tx_version /*= 1*/,
                                                             uint8_t script_version /*= 5*/) {
    chain::transaction tx;
    tx.set_version(tx_version);
    tx.set_locktime(locktime);

    for (auto const& input : outputs_to_spend) {
        //TODO(kth): move the elements instead of pushing back
        tx.inputs().push_back(config::input(input));
    }

    for (auto const& output : destiny_and_amount) {
        std::string destiny_string = output.first.encoded_legacy() + ":" + std::to_string(output.second);
        if ( ! push_scripts(tx.outputs(), config::output(destiny_string), script_version)) {
            return {error::error_code_t::invalid_output, {}};
        }
    }

    for (auto const& data : extra_outputs) {
        tx.outputs().push_back(data);
    }

    if (tx.is_locktime_conflict()) {
        return {error::error_code_t::lock_time_conflict, {}};
    }

    return {error::error_code_t::success, tx};
}

std::pair<error::error_code_t, chain::transaction> tx_encode(chain::input_point::list const& outputs_to_spend,
                                                             raw_output_list const& destiny_and_amount,
                                                             uint32_t locktime /*= 0*/,
                                                             uint32_t tx_version /*= 1*/,
                                                             uint8_t script_version /*= 5*/) {
    return tx_encode(outputs_to_spend, destiny_and_amount, chain::output::list{}, locktime, tx_version, script_version);
}

//https://github.com/libbitcoin/libbitcoin-explorer/blob/master/src/commands/input-sign.cpp
std::pair<error::error_code_t, data_chunk> input_signature_old(kth::ec_secret const& private_key,
                                                               chain::script const& output_script,
                                                               chain::transaction const& tx,
                                                               uint32_t index,
                                                               uint8_t sign_type /*= 0x01*/,
                                                               bool anyone_can_pay /*= false*/) {
    if (index >= tx.inputs().size()) {
        return {error::error_code_t::input_index_out_of_range, {}};
    }

    if (anyone_can_pay) {
        sign_type |= infrastructure::machine::sighash_algorithm::anyone_can_pay;
    }

    kth::endorsement endorse;
    if ( ! chain::script::create_endorsement(endorse, private_key, output_script, tx, index, sign_type)) {
        return {error::error_code_t::input_sign_failed, {}};
    }
    return {error::error_code_t::success, endorse};
}

//https://github.com/libbitcoin/libbitcoin-explorer/blob/master/src/commands/input-sign.cpp
std::pair<error::error_code_t, data_chunk> input_signature_btc(kth::ec_secret const& private_key,
                                                               chain::script const& output_script,
                                                               chain::transaction const& tx,
                                                               uint64_t amount,
                                                               uint32_t index,
                                                               uint8_t sign_type /*= 0x01*/,
                                                               bool anyone_can_pay /*= false*/) {
    if (index >= tx.inputs().size()) {
        return {error::error_code_t::input_index_out_of_range, {}};
    }

    if (anyone_can_pay) {
        sign_type |= infrastructure::machine::sighash_algorithm::anyone_can_pay;
    }

    kth::endorsement endorse;

    if ( ! chain::script::create_endorsement(endorse,
                                                       private_key,
                                                       output_script,
                                                       tx,
                                                       index,
                                                       sign_type,
                                                       chain::script::script_version::zero,
                                                       amount)) {
        return {error::error_code_t::input_sign_failed, {}};
    }

    return {error::error_code_t::success, endorse};
}

std::pair<error::error_code_t, data_chunk> input_signature_bch(kth::ec_secret const& private_key,
                                                               chain::script const& output_script,
                                                               chain::transaction const& tx,
                                                               uint64_t amount,
                                                               uint32_t index,
                                                               uint8_t sign_type /*= 0x01*/,
                                                               bool anyone_can_pay /*= false*/) {
    //// NOTE: BCH endorsement uses script_version::zero (segwit sign algorithm)
    //// Add FORK_ID to the sign_type when signing bitcoin cash transactions
    sign_type |= 0x40U;
    return input_signature_btc(private_key, output_script, tx, amount, index, sign_type, anyone_can_pay);
}

//https://github.com/libbitcoin/libbitcoin-explorer/blob/master/src/commands/input-set.cpp
std::pair<error::error_code_t, chain::transaction> input_set(chain::script const& script,
                                                             chain::transaction const& raw_tx,
                                                             uint32_t index /*= 0*/) {
    if (index >= raw_tx.inputs().size()) {
        return {error::error_code_t::input_index_out_of_range, {}};
    }

    // Clone so we keep arguments const.
    chain::transaction tx_out(raw_tx);

    tx_out.inputs()[index].set_script(script);

    return {error::error_code_t::success, tx_out};
}

// Using the signature and public key it generates the script
std::pair<error::error_code_t, chain::transaction> input_set(data_chunk const& signature,
                                                             wallet::ec_public const& public_key,
                                                             chain::transaction const& raw_tx,
                                                             uint32_t index /*= 0*/) {
    config::script script("[" + kth::encode_base16(signature) + "] [" + public_key.encoded() + "]");
    return input_set(script, raw_tx, index);
}

}  //namespace kth::domain::wallet