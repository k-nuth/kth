// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b)
// src/policy/policy.cpp and Solver() of src/script/standard.cpp.
// Original: Copyright (c) 2009-2010 Satoshi Nakamoto,
//           Copyright (c) 2009-2016 The Bitcoin Core developers,
//           Copyright (c) 2020-2025 The Bitcoin developers, MIT license.

#if defined(KTH_CURRENCY_BCH)
#include <kth/domain/policy/standardness_bchn.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kth::domain::policy {

using machine::script_flags;

namespace {

// Raw opcodes, the script is walked byte by byte like BCHN's CScript::GetOp.
constexpr uint8_t op_0 = 0x00;
constexpr uint8_t op_pushdata1 = 0x4c;
constexpr uint8_t op_pushdata2 = 0x4d;
constexpr uint8_t op_pushdata4 = 0x4e;
constexpr uint8_t op_1 = 0x51;
constexpr uint8_t op_16 = 0x60;
constexpr uint8_t op_return = 0x6a;
constexpr uint8_t op_dup = 0x76;
constexpr uint8_t op_equal = 0x87;
constexpr uint8_t op_equalverify = 0x88;
constexpr uint8_t op_hash160 = 0xa9;
constexpr uint8_t op_hash256 = 0xaa;
constexpr uint8_t op_checksig = 0xac;
constexpr uint8_t op_checkmultisig = 0xae;

constexpr size_t compressed_public_key_size = 33;
constexpr size_t uncompressed_public_key_size = 65;
constexpr size_t hash160_size = 20;
constexpr size_t hash256_size = 32;

// BCHN: opcodeRet starts as INVALIDOPCODE (0xff) and stays so if GetOp fails.
constexpr int invalid_opcode = 0xff;

using bytes_view = std::span<uint8_t const>;

struct op_read {
    bool ok = false;
    int opcode = invalid_opcode;
    bytes_view data;
    size_t next = 0;
};

// BCHN GetScriptOp.
op_read get_op(bytes_view script, size_t pos) {
    op_read result;
    result.next = pos;
    if (pos >= script.size()) {
        return result;
    }

    uint32_t const opcode = script[pos++];
    bytes_view data;

    if (opcode <= op_pushdata4) {
        uint32_t size = 0;
        size_t const remaining = script.size() - pos;
        if (opcode < op_pushdata1) {
            size = opcode;
        } else if (opcode == op_pushdata1) {
            if (remaining < 1) return result;
            size = script[pos];
            pos += 1;
        } else if (opcode == op_pushdata2) {
            if (remaining < 2) return result;
            size = uint32_t(script[pos]) | (uint32_t(script[pos + 1]) << 8);
            pos += 2;
        } else {
            if (remaining < 4) return result;
            size = uint32_t(script[pos]) | (uint32_t(script[pos + 1]) << 8)
                 | (uint32_t(script[pos + 2]) << 16) | (uint32_t(script[pos + 3]) << 24);
            pos += 4;
        }
        if (script.size() - pos < size) {
            return result;
        }
        data = script.subspan(pos, size);
        pos += size;
    }

    result.ok = true;
    result.opcode = int(opcode);
    result.data = data;
    result.next = pos;
    return result;
}

// BCHN CScript::IsPushOnly(const_iterator): note OP_RESERVED (0x50) counts as a push.
bool is_push_only(bytes_view script, size_t pos) {
    while (pos < script.size()) {
        auto const op = get_op(script, pos);
        if (!op.ok || op.opcode > op_16) {
            return false;
        }
        pos = op.next;
    }
    return true;
}

// BCHN CheckMinimalPush, opcode in OP_0..OP_PUSHDATA4.
bool check_minimal_push(bytes_view data, int opcode) {
    if (data.empty()) {
        return opcode == op_0;
    }
    if (data.size() == 1 && data[0] >= 1 && data[0] <= 16) {
        return false;
    }
    if (data.size() == 1 && data[0] == 0x81) {
        return false;
    }
    if (data.size() <= 75) {
        return size_t(opcode) == data.size();
    }
    if (data.size() <= 255) {
        return opcode == op_pushdata1;
    }
    if (data.size() <= 65535) {
        return opcode == op_pushdata2;
    }
    return true;
}

// BCHN CPubKey::ValidSize: the header byte must agree with the length.
bool valid_public_key_size(bytes_view key) {
    if (key.empty()) {
        return false;
    }
    auto const header = key[0];
    if (header == 2 || header == 3) {
        return key.size() == compressed_public_key_size;
    }
    if (header == 4 || header == 6 || header == 7) {
        return key.size() == uncompressed_public_key_size;
    }
    return false;
}

constexpr bool is_small_integer(int opcode) {
    return opcode >= op_1 && opcode <= op_16;
}

constexpr unsigned decode_op_n(int opcode) {
    return unsigned(opcode - (op_1 - 1));
}

bool match_pay_to_pubkey(bytes_view script) {
    if (script.size() == uncompressed_public_key_size + 2 &&
        script[0] == uncompressed_public_key_size && script.back() == op_checksig) {
        return valid_public_key_size(script.subspan(1, uncompressed_public_key_size));
    }
    if (script.size() == compressed_public_key_size + 2 &&
        script[0] == compressed_public_key_size && script.back() == op_checksig) {
        return valid_public_key_size(script.subspan(1, compressed_public_key_size));
    }
    return false;
}

bool match_pay_to_pubkey_hash(bytes_view script) {
    return script.size() == 25 && script[0] == op_dup && script[1] == op_hash160 &&
           script[2] == hash160_size && script[23] == op_equalverify && script[24] == op_checksig;
}

// BCHN MatchMultisig.
bool match_multisig(bytes_view script, unsigned& required, size_t& public_keys) {
    if (script.empty() || script.back() != op_checkmultisig) {
        return false;
    }

    auto op = get_op(script, 0);
    if (!op.ok || !is_small_integer(op.opcode)) {
        return false;
    }
    required = decode_op_n(op.opcode);
    size_t pos = op.next;
    public_keys = 0;

    while (true) {
        op = get_op(script, pos);
        if (!op.ok || !valid_public_key_size(op.data)) {
            break;
        }
        // The key is a push (data is only non-empty for pushes).
        if (!check_minimal_push(op.data, op.opcode)) {
            return false;
        }
        ++public_keys;
        pos = op.next;
    }

    if (!is_small_integer(op.opcode)) {
        return false;
    }
    unsigned const keys = decode_op_n(op.opcode);
    if (public_keys != keys || keys < required) {
        return false;
    }
    // Only OP_CHECKMULTISIG may follow.
    return op.next + 1 == script.size();
}

bool is_p2sh_160(bytes_view script) {
    return script.size() == 23 && script[0] == op_hash160 && script[1] == hash160_size && script[22] == op_equal;
}

bool is_p2sh_256(bytes_view script, script_flags_t flags) {
    return (flags & script_flags::bch_p2sh_32) != 0 &&
           script.size() == 35 && script[0] == op_hash256 && script[1] == hash256_size && script[34] == op_equal;
}

// BCHN CScript::IsUnspendable.
bool is_unspendable(bytes_view script) {
    return (!script.empty() && script[0] == op_return) || script.size() > bchn_max_script_size;
}

// BCHN CFeeRate::GetFee: floor(size * rate / 1000), but at least 1 satoshi for a positive rate and size.
uint64_t get_fee(uint64_t size, uint64_t fee_per_kb) {
    uint64_t fee = (size / 1000) * fee_per_kb + ((size % 1000) * fee_per_kb) / 1000;
    if (fee == 0 && size != 0 && fee_per_kb > 0) {
        fee = 1;
    }
    return fee;
}

bytes_view view(chain::script const& script) {
    auto const& bytes = script.bytes();
    return {bytes.data(), bytes.size()};
}

// BCHN IsStandard(scriptPubKey, whichType, flags).
bool is_standard_script(chain::script const& script, script_flags_t flags, bchn_script_type& type) {
    auto const solution = solve_script_bchn(script, flags);
    type = solution.type;
    if (solution.type == bchn_script_type::nonstandard) {
        return false;
    }
    if (solution.type == bchn_script_type::multisig) {
        // Support up to x-of-3 multisig txns as standard
        if (solution.keys < 1 || solution.keys > bchn_max_standard_multisig_keys) {
            return false;
        }
        if (solution.required < 1 || solution.required > solution.keys) {
            return false;
        }
    }
    return true;
}

} // namespace

// Flags.
//-----------------------------------------------------------------------------

script_flags_t bchn_mandatory_script_flags() {
    return script_flags::bip16_rule | script_flags::bch_strictenc | script_flags::bch_sighash_forkid
         | script_flags::bch_low_s | script_flags::bch_nullfail | script_flags::bch_minimaldata
         | script_flags::bch_schnorr_multisig | script_flags::bch_enforce_sigchecks;
}

script_flags_t bchn_standard_script_flags() {
    return bchn_mandatory_script_flags() | script_flags::bip66_rule | script_flags::bch_sigpushonly
         | script_flags::bch_discourage_upgradable_nops | script_flags::bch_cleanstack
         | script_flags::bip65_rule | script_flags::bip112_rule
         | script_flags::bch_disallow_segwit_recovery | script_flags::bch_input_sigchecks;
}

script_flags_t get_standard_script_flags_bchn(script_flags_t consensus_flags, bool require_standard) {
    script_flags_t extra = 0;
    if ((consensus_flags & script_flags::bch_vm_limits) != 0 && require_standard) {
        extra |= script_flags::bch_vm_limits_standard;
    }
    return consensus_flags | bchn_standard_script_flags() | extra;
}

// Solver.
//-----------------------------------------------------------------------------

bchn_solution solve_script_bchn(chain::script const& script, script_flags_t flags) {
    auto const bytes = view(script);
    bchn_solution solution;

    if (is_p2sh_160(bytes) || is_p2sh_256(bytes, flags)) {
        solution.type = bchn_script_type::scripthash;
        return solution;
    }

    if (!bytes.empty() && bytes[0] == op_return && is_push_only(bytes, 1)) {
        solution.type = bchn_script_type::null_data;
        return solution;
    }

    if (match_pay_to_pubkey(bytes)) {
        solution.type = bchn_script_type::pubkey;
        return solution;
    }

    if (match_pay_to_pubkey_hash(bytes)) {
        solution.type = bchn_script_type::pubkeyhash;
        return solution;
    }

    unsigned required = 0;
    size_t keys = 0;
    if (match_multisig(bytes, required, keys)) {
        solution.type = bchn_script_type::multisig;
        solution.required = uint8_t(required);
        solution.keys = uint8_t(keys);
        return solution;
    }

    // After upgrade 12 (p2s), any other script of up to 201 bytes is standard.
    if ((flags & script_flags::bch_p2s) != 0 && bytes.size() <= bchn_max_p2s_script_size) {
        solution.type = bchn_script_type::script;
        return solution;
    }

    return solution;
}

// Dust and sizes.
//-----------------------------------------------------------------------------

uint64_t get_dust_threshold_bchn(chain::output const& output, uint64_t dust_relay_fee_per_kb) {
    if (is_unspendable(view(output.script()))) {
        return 0;
    }
    auto const size = output.serialized_size(true) + bchn_dust_spend_input_size;
    return 3 * get_fee(size, dust_relay_fee_per_kb);
}

bool is_dust_bchn(chain::output const& output, uint64_t dust_relay_fee_per_kb) {
    return output.value() < get_dust_threshold_bchn(output, dust_relay_fee_per_kb);
}

int64_t get_virtual_transaction_size_bchn(int64_t size, int64_t sigchecks, uint32_t bytes_per_sigcheck) {
    return std::max<int64_t>(size, sigchecks * int64_t(bytes_per_sigcheck));
}

int64_t get_virtual_transaction_size_bchn(chain::transaction const& tx, int64_t sigchecks, uint32_t bytes_per_sigcheck) {
    return get_virtual_transaction_size_bchn(int64_t(tx.serialized_size(true)), sigchecks, bytes_per_sigcheck);
}

int64_t get_virtual_transaction_input_size_bchn(chain::input const& input, int64_t sigchecks, uint32_t bytes_per_sigcheck) {
    return get_virtual_transaction_size_bchn(int64_t(input.serialized_size(true)), sigchecks, bytes_per_sigcheck);
}

// Standardness.
//-----------------------------------------------------------------------------

code check_script_standardness_bchn(chain::script const& script, script_flags_t flags) {
    bchn_script_type type;
    return is_standard_script(script, flags, type) ? code(error::success) : code(error::standardness_scriptpubkey);
}

code check_transaction_standardness_bchn(chain::transaction const& tx, script_flags_t flags,
                                         bchn_standardness_parameters const& parameters) {
    // Note that this may be removed after upgrade 9, where version 1 or 2 is consensus.
    if (tx.version() > bchn_max_standard_version || tx.version() < bchn_min_standard_version) {
        return error::standardness_version;
    }

    if (tx.serialized_size(true) > bchn_max_standard_tx_size) {
        return error::standardness_transaction_size;
    }

    // Before upgrade 12 scriptSigs were limited to 1650 bytes, afterwards to the maximum script size.
    auto const max_script_sig_size = (flags & script_flags::bch_p2s) == 0 ? bchn_max_tx_in_script_sig_size_legacy
                                                                           : bchn_max_script_size;

    for (auto const& input : tx.inputs()) {
        auto const script_sig = view(input.script());
        if (script_sig.size() > max_script_sig_size) {
            return error::standardness_scriptsig_size;
        }
        if (!is_push_only(script_sig, 0)) {
            return error::standardness_scriptsig_not_push_only;
        }
    }

    size_t data_size = 0;
    for (auto const& output : tx.outputs()) {
        if ((flags & script_flags::bch_tokens) == 0 && output.token_data().has_value()) {
            return error::standardness_tokens_before_activation;
        }

        bchn_script_type type;
        if (!is_standard_script(output.script(), flags, type)) {
            return error::standardness_scriptpubkey;
        }

        if (type == bchn_script_type::null_data) {
            data_size += output.script().bytes().size();
        } else if (type == bchn_script_type::multisig && !parameters.permit_bare_multisig) {
            return error::standardness_bare_multisig;
        } else if (is_dust_bchn(output, parameters.dust_relay_fee_per_kb)) {
            return error::standardness_dust;
        }
    }

    if (data_size > parameters.maximum_datacarrier_bytes) {
        return error::standardness_datacarrier_size;
    }

    return error::success;
}

code check_input_standardness_bchn(chain::transaction const& tx, script_flags_t flags) {
    if (tx.is_coinbase()) {
        return error::success;
    }

    for (auto const& input : tx.inputs()) {
        auto const& prevout = input.previous_output().validation.cache;
        if (!prevout.is_valid()) {
            return error::missing_previous_output;
        }

        if ((flags & script_flags::bch_tokens) == 0 && prevout.token_data().has_value()) {
            return error::standardness_inputs;
        }

        if (solve_script_bchn(prevout.script(), flags).type == bchn_script_type::nonstandard) {
            return error::standardness_inputs;
        }
    }

    return error::success;
}

} // namespace kth::domain::policy

#endif // KTH_CURRENCY_BCH
