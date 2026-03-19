// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/consensus.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string.h>

#include <kth/consensus/conversions.hpp>
#include <kth/consensus/define.hpp>
#include <kth/consensus/export.hpp>

#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/interpreter.h"
#include "script/script_error.h"
#include "streams.h"
#include "version.h"

#if defined(KTH_CURRENCY_BCH)
#include "script/script_metrics.h"
#endif

namespace kth::consensus {

// Initialize libsecp256k1 context.
static auto secp256k1_context = ECCVerifyHandle();

// Helper class, not published. This is tested internal to verify_script.
class transaction_istream {
public:
    template<typename Type>
    transaction_istream& operator>>(Type& instance) {
        ::Unserialize(*this, instance);
        return *this;
    }

    transaction_istream(uint8_t const* transaction, size_t size)
        : source_(transaction), remaining_(size) {}

    void read(char* destination, size_t size) {
        if (size > remaining_) {
            throw std::ios_base::failure("end of data");
        }

        memcpy(destination, source_, size);
        remaining_ -= size;
        source_ += size;
    }

    int GetType() const {
        return SER_NETWORK;
    }

    int GetVersion() const {
        return PROTOCOL_VERSION;
    }

private:
    size_t remaining_;
    uint8_t const* source_;
};

// This mapping decouples the consensus API from the satoshi implementation
// files. We prefer to keep our copies of consensus files isomorphic.
// This function is not published (but non-static for testability).

verify_result_type script_error_to_verify_result(ScriptError code) {
    switch (code) {
        // Logical result
        case ScriptError::OK:
            return verify_result_eval_true;
        case ScriptError::EVAL_FALSE:
            return verify_result_eval_false;

        // Max size errors
        case ScriptError::SCRIPT_SIZE:
            return verify_result_script_size;
        case ScriptError::PUSH_SIZE:
            return verify_result_push_size;
        case ScriptError::OP_COUNT:
            return verify_result_op_count;
        case ScriptError::STACK_SIZE:
            return verify_result_stack_size;
        case ScriptError::SIG_COUNT:
            return verify_result_sig_count;
        case ScriptError::PUBKEY_COUNT:
            return verify_result_pubkey_count;

        // Failed verify operations
        case ScriptError::VERIFY:
            return verify_result_verify;
        case ScriptError::EQUALVERIFY:
            return verify_result_equalverify;
        case ScriptError::CHECKMULTISIGVERIFY:
            return verify_result_checkmultisigverify;
        case ScriptError::CHECKSIGVERIFY:
            return verify_result_checksigverify;
        case ScriptError::NUMEQUALVERIFY:
            return verify_result_numequalverify;

        // Logical/Format/Canonical errors
        case ScriptError::BAD_OPCODE:
            return verify_result_bad_opcode;
        case ScriptError::DISABLED_OPCODE:
            return verify_result_disabled_opcode;
        case ScriptError::INVALID_STACK_OPERATION:
            return verify_result_invalid_stack_operation;
        case ScriptError::INVALID_ALTSTACK_OPERATION:
            return verify_result_invalid_altstack_operation;
        case ScriptError::UNBALANCED_CONDITIONAL:
            return verify_result_unbalanced_conditional;

        // BIP65/BIP112 (shared codes)
        case ScriptError::NEGATIVE_LOCKTIME:
            return verify_result_negative_locktime;
        case ScriptError::UNSATISFIED_LOCKTIME:
            return verify_result_unsatisfied_locktime;

        // BIP62
        case ScriptError::SIG_HASHTYPE:
            return verify_result_sig_hashtype;
        case ScriptError::SIG_DER:
            return verify_result_sig_der;
        case ScriptError::MINIMALDATA:
            return verify_result_minimaldata;
        case ScriptError::SIG_PUSHONLY:
            return verify_result_sig_pushonly;
        case ScriptError::SIG_HIGH_S:
            return verify_result_sig_high_s;
        case ScriptError::PUBKEYTYPE:
            return verify_result_pubkeytype;
        case ScriptError::CLEANSTACK:
            return verify_result_cleanstack;
        case ScriptError::MINIMALIF:
            return verify_result_minimalif;
        case ScriptError::SIG_NULLFAIL:
            return verify_result_sig_nullfail;
        case ScriptError::MINIMALNUM:
            return verify_result_minimalnum;

        // Softfork safeness
        case ScriptError::DISCOURAGE_UPGRADABLE_NOPS:
            return verify_result_discourage_upgradable_nops;

        case ScriptError::INPUT_SIGCHECKS:
            return verify_result_input_sigchecks;

        case ScriptError::INVALID_OPERAND_SIZE:
            return verify_result_invalid_operand_size;
        case ScriptError::INVALID_NUMBER_RANGE:
            return verify_result_invalid_number_range;
        case ScriptError::IMPOSSIBLE_ENCODING:
            return verify_result_impossible_encoding;
        case ScriptError::INVALID_SPLIT_RANGE:
            return verify_result_invalid_split_range;
        case ScriptError::INVALID_BIT_COUNT:
            return verify_result_invalid_bit_count;

        case ScriptError::CHECKDATASIGVERIFY:
            return verify_result_checkdatasigverify;

        case ScriptError::DIV_BY_ZERO:
            return verify_result_div_by_zero;
        case ScriptError::MOD_BY_ZERO:
            return verify_result_mod_by_zero;

        case ScriptError::INVALID_BITFIELD_SIZE:
            return verify_result_invalid_bitfield_size;
        case ScriptError::INVALID_BIT_RANGE:
            return verify_result_invalid_bit_range;

        case ScriptError::SIG_BADLENGTH:
            return verify_result_sig_badlength;
        case ScriptError::SIG_NONSCHNORR:
            return verify_result_sig_nonschnorr;

        case ScriptError::ILLEGAL_FORKID:
            return verify_result_illegal_forkid;
        case ScriptError::MUST_USE_FORKID:
            return verify_result_must_use_forkid;

        case ScriptError::SIGCHECKS_LIMIT_EXCEEDED:
            return verify_result_sigchecks_limit_exceeded;

        // Other
        case ScriptError::OP_RETURN:
            return verify_result_op_return;
        case ScriptError::UNKNOWN:
        case ScriptError::ERROR_COUNT:
        default:
            return verify_result_unknown_error;
    }
}

// This function is published. The implementation exposes no satoshi internals.
verify_result_type verify_script(
    unsigned char const* transaction,
    size_t transaction_size,
    unsigned char const* locking_script_data,
    size_t locking_script_size,
    unsigned char const* unlocking_script_data,
    size_t unlocking_script_size,
    unsigned int tx_input_index,
    unsigned int flags,
    size_t& sig_checks,
    int64_t amount,
    std::vector<std::vector<uint8_t>> coins) {

    if (amount > INT64_MAX) {
        throw std::invalid_argument("value");
    }

    if (transaction_size > 0 && transaction == nullptr) {
        throw std::invalid_argument("transaction");
    }

    if (locking_script_size > 0 && locking_script_data == nullptr) {
        throw std::invalid_argument("locking_script_data");
    }

    if (unlocking_script_size > 0 && unlocking_script_data == nullptr) {
        throw std::invalid_argument("unlocking_script_data");
    }

    std::optional<CTransaction> txopt;

    try {
        transaction_istream stream(transaction, transaction_size);
        txopt.emplace(deserialize, stream);
    }
    catch (const std::exception&) {
        return verify_result_tx_invalid;
    }

    if ( ! txopt) {
        return verify_result_tx_invalid;
    }

    auto const& tx = *txopt;

    if (tx_input_index >= tx.vin.size()) {
        return verify_result_tx_input_invalid;
    }

    if (GetSerializeSize(tx, PROTOCOL_VERSION) != transaction_size) {
        return verify_result_tx_size_invalid;
    }

    ScriptError error;
    Amount am(amount);
    const unsigned int script_flags = verify_flags_to_script_flags(flags);

    CScript const locking_script(locking_script_data, locking_script_data + locking_script_size);
    CScript const unlocking_script(unlocking_script_data, unlocking_script_data + unlocking_script_size);
    // auto const& unlocking_script = tx.vin[tx_input_index].scriptSig;

    ScriptExecutionMetrics metrics = {};

    if ( ! coins.empty()) {
        auto const output_getter = [&coins](size_t i) {
            auto const& data = coins.at(i);
            CDataStream stream(data, SER_NETWORK, PROTOCOL_VERSION);
            CTxOut ret;
            ::Unserialize(stream, ret);
            return ret;
        };

        auto const contexts = ScriptExecutionContext::createForAllInputs(tx, output_getter);

        if (tx_input_index >= contexts.size()) {
            return verify_result_tx_input_invalid;
        }
        auto const context = contexts[tx_input_index];
        PrecomputedTransactionData txdata(context);
        TransactionSignatureChecker checker(context, txdata);
        VerifyScript(unlocking_script, locking_script, script_flags, checker, metrics, &error);
    } else {
        ScriptExecutionContextOpt context = std::nullopt;
        ContextOptSignatureChecker checker(context);
        VerifyScript(unlocking_script, locking_script, script_flags, checker, metrics, &error);
    }

    sig_checks = metrics.nSigChecks;
    return script_error_to_verify_result(error);
}

#if defined(KTH_CURRENCY_BCH)

namespace {

// A special pubkey that causes signature checks to return false.
static std::vector<uint8_t> const badpub_key = {
    2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// StandaloneSigChecker: real ECDSA/Schnorr verification for CHECKDATASIG
// (uses BaseSignatureChecker::VerifySignature which calls secp256k1).
// CheckSig remains dummy because CHECKSIG requires transaction context.
struct StandaloneSigChecker : BaseSignatureChecker {
    bool CheckSig(ByteView const& vchSigIn,
                  std::vector<uint8_t> const& vchPubKey,
                  ByteView const& scriptCode,
                  uint32_t /*flags*/,
                  size_t* pbytesHashed) const final {
        if (pbytesHashed) *pbytesHashed = 0u;
        if (vchSigIn.empty()) return false;
        if (pbytesHashed) {
            *pbytesHashed = vchSigIn.size() + vchPubKey.size() + scriptCode.size();
        }
        return vchPubKey != badpub_key;
    }
};

static StandaloneSigChecker const standalone_sig_checker;

} // anonymous namespace

verify_result_type eval_script_with_metrics(
    unsigned char const* script_data,
    size_t script_size,
    unsigned int flags,
    std::vector<std::vector<uint8_t>>& stack,
    script_eval_metrics& metrics_out) {

    if (script_size > 0 && script_data == nullptr) {
        throw std::invalid_argument("script_data");
    }

    unsigned int const script_flags = verify_flags_to_script_flags(flags);
    CScript const script(script_data, script_data + script_size);

    ScriptExecutionMetrics metrics;
    ScriptError error;

    EvalScript(stack, script, script_flags, standalone_sig_checker, metrics, &error);

    metrics_out.sig_checks = metrics.GetSigChecks();
    metrics_out.op_cost = metrics.GetBaseOpCost();
    metrics_out.hash_digest_iterations = metrics.GetHashDigestIterations();
    return script_error_to_verify_result(error);
}

#endif // KTH_CURRENCY_BCH

} // namespace kth::consensus
