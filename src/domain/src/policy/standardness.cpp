// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <kth/domain/policy/standardness.hpp>

#if defined(KTH_CURRENCY_BCH)
namespace kth::domain::policy {

code check_script_standardness(chain::script const& script,
                              transaction_standardness_parameters const& parameters) {
    if (script.serialized_size(false) > parameters.maximum_output_script_size)
        return error::standardness_scriptpubkey;
    return error::success;
}

code check_input_standardness(chain::transaction const& tx,
                             transaction_standardness_parameters const& parameters) {
    for (auto const& input : tx.inputs()) {
        if (input.script().serialized_size(false) > parameters.maximum_scriptsig_size)
            return error::standardness_scriptsig_size;
        if (parameters.require_push_only && !chain::script::is_relaxed_push(input.script().operations()))
            return error::standardness_scriptsig_not_push_only;
    }
    return error::success;
}

code check_transaction_standardness(chain::transaction const& tx,
                                   transaction_standardness_parameters const& parameters) {
    if (tx.version() < parameters.minimum_version || tx.version() > parameters.maximum_version)
        return error::standardness_version;
    if (tx.serialized_size(true) > parameters.maximum_transaction_size)
        return error::standardness_transaction_size;
    if (auto const ec = check_input_standardness(tx, parameters)) return ec;
    size_t data_bytes = 0;
    for (auto const& output : tx.outputs()) {
        auto const& script = output.script();
        if (auto const ec = check_script_standardness(script, parameters)) return ec;
        auto const& bytes = script.bytes();
        if (!bytes.empty() && bytes.front() == uint8_t(machine::opcode::return_)) {
            data_bytes += bytes.size();
        }
        if (output.value() < parameters.minimum_output_value && !script.is_unspendable())
            return error::standardness_dust;
    }
    if (data_bytes > parameters.maximum_datacarrier_bytes)
        return error::standardness_datacarrier_size;
    return error::success;
}

} // namespace kth::domain::policy
#endif
