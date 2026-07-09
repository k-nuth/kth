// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/script.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <numeric>
#include <sstream>
#include <utility>

#include <boost/range/adaptor/reversed.hpp>

#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/machine/script_pattern.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/string.hpp>

using namespace kth::domain::machine;
using namespace kth::infrastructure::machine;
using namespace boost::adaptors;

namespace kth::domain::chain {

// bit.ly/2cPazSa
static
auto const one_hash = "0000000000000000000000000000000000000000000000000000000000000001"_hash; //NOLINT

// Constructors.
//-----------------------------------------------------------------------------

script::script(operation::list const& ops) {
    from_operations(ops);
}

script::script(operation::list&& ops) {
    from_operations(std::move(ops));
}

script::script(data_chunk&& encoded, bool prefix) {
    if (prefix) {
        auto obj = kth::from_data_chunk<script>(encoded, prefix);
        if ( ! obj) {
            valid_ = false;
            return;
        }
        valid_ = true;
        *this = std::move(obj.value());
        return;
    }

    // This is an optimization that avoids streaming the encoded bytes.
    bytes_ = std::move(encoded);
    valid_ = true;
}

script::script(data_chunk const& encoded, bool prefix) {
    auto obj = kth::from_data_chunk<script>(encoded, prefix);
    if ( ! obj) {
        valid_ = false;
        return;
    }
    valid_ = true;
    *this = std::move(obj.value());
}

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<script> script::from_data(byte_reader& reader, bool prefix) {
    if ( ! prefix) {
        auto const bytes = reader.read_remaining_bytes();
        if ( ! bytes) {
            return std::unexpected(bytes.error());
        }
        return script {data_chunk(std::begin(*bytes), std::end(*bytes)), false};
    }

    auto const size = reader.read_size_little_endian();
    if ( ! size) {
        return std::unexpected(size.error());
    }

    // The max_script_size constant limits evaluation, but not all scripts
    // evaluate, so use max_block_size to guard memory allocation here.
    if (*size > static_absolute_max_block_size()) {
        return std::unexpected(error::script_invalid_size);
    }
    auto const bytes = reader.read_bytes(*size);
    if ( ! bytes) {
        return std::unexpected(bytes.error());
    }
    return script {data_chunk(std::begin(*bytes), std::end(*bytes)), false};
}

// static
expect<script> script::from_data_with_size(byte_reader& reader, size_t size) {
    // The max_script_size constant limits evaluation, but not all scripts evaluate, so use max_block_size to guard memory allocation here.
    if (size > static_absolute_max_block_size()) {
        return std::unexpected(error::script_invalid_size);
    }

    auto const bytes = reader.read_bytes(size);
    if ( ! bytes) {
        return std::unexpected(bytes.error());
    }
    return script {data_chunk(std::begin(*bytes), std::end(*bytes)), false};
}

bool script::from_string(std::string const& mnemonic) {
    reset();

    auto const tokens = split(mnemonic);
    operation::list ops;
    ops.resize(tokens.empty() || tokens.front().empty() ? 0 : tokens.size());

    for (size_t index = 0; index < ops.size(); ++index) {
        if ( ! ops[index].from_string(tokens[index])) {
            return false;
        }
    }

    from_operations(ops);
    return true;
}

void script::from_operations(operation::list const& ops) {
    bytes_ = operations_to_data(ops);
    valid_ = true;
}

void script::from_operations(operation::list&& ops) {
    bytes_ = operations_to_data(ops);
    valid_ = true;
}

// private/static
data_chunk script::operations_to_data(operation::list const& ops) {
    data_chunk out;
    auto const size = serialized_size(ops);
    out.reserve(size);
    auto const concatenate = [&out](operation const& op) {
        auto bytes = op.to_data();
        std::move(bytes.begin(), bytes.end(), std::back_inserter(out));
    };

    std::for_each(ops.begin(), ops.end(), concatenate);
    KTH_ASSERT(out.size() == size);
    return out;
}

// private/static
size_t script::serialized_size(operation::list const& ops) {
    auto const op_size = [](size_t total, operation const& op) {
        return total + op.serialized_size();
    };

    return std::accumulate(ops.begin(), ops.end(), size_t{0}, op_size);
}

void script::reset() {
    bytes_.clear();
    bytes_.shrink_to_fit();
    valid_ = false;
}

bool script::is_valid() const {
    return valid_;
}

bool script::is_valid_operations() const {
    auto const ops = operations();
    // Script validity is independent of individual operation validity.
    // There is a trailing invalid/default op if a push op had a size mismatch.
    return ops.empty() || ops.back().is_valid();
}

expect<void> script::to_data(byte_writer& writer, bool prefix) const {
    // TODO(legacy): optimize by always storing the prefixed serialization.
    if (prefix) {
        if (auto r = writer.write_variable_little_endian(serialized_size(false)); ! r) {
            return r;
        }
    }
    return writer.write_bytes(bytes_);
}

std::string script::to_string(script_flags_t active_flags) const {
    auto first = true;
    std::ostringstream text;

    for (auto const& op : operations()) {
        text << (first ? "" : " ") << op.to_string(active_flags);
        first = false;
    }

    return text.str();
}

// Properties (size, accessors).
//-----------------------------------------------------------------------------

size_t script::serialized_size(bool prefix) const {
    auto size = bytes_.size();

    if (prefix) {
        size += infrastructure::message::variable_uint_size(size);
    }

    return size;
}

data_chunk const& script::bytes() const {
    return bytes_;
}

operation::list script::operations() const {
    return chain::operations(*this);
}

operation script::first_operation() const {
    return chain::first_operation(*this);
}

// Signing (unversioned).
//-----------------------------------------------------------------------------

inline
std::pair<hash_digest, size_t> signature_hash(transaction const& tx, uint32_t sighash_type) {
    KTH_ASSERT( ! tx.is_coinbase());

    auto serialized = to_data_chunk(tx, true);
    extend_data(serialized, to_little_endian(sighash_type));
    return {bitcoin_hash(serialized), serialized.size()};
}

//*****************************************************************************
// CONSENSUS: Due to masking of bits 6/7 (8 is the anyone_can_pay flag),
// there are 4 possible 7 bit values that can set "single" and 4 others that
// can set none, and yet all other values set "all".
//*****************************************************************************
inline
sighash_algorithm to_sighash_enum(uint8_t sighash_type) {
    switch (sighash_type & sighash_algorithm::mask) {
        case sighash_algorithm::single:
            return sighash_algorithm::single;
        case sighash_algorithm::none:
            return sighash_algorithm::none;
        default:
            return sighash_algorithm::all;
    }
}

inline
uint8_t is_sighash_enum(uint8_t sighash_type, sighash_algorithm value) {
    return uint8_t(
        to_sighash_enum(sighash_type) == value
    );
}

static
std::pair<hash_digest, size_t> sign_none(transaction const& tx, uint32_t input_index, script const& script_code, uint8_t sighash_type) {
    input::list ins;
    auto const& inputs = tx.inputs();
    auto const any = (sighash_type & sighash_algorithm::anyone_can_pay) != 0;
    ins.reserve(any ? 1 : inputs.size());

    KTH_ASSERT(input_index < inputs.size());
    auto const& self = inputs[input_index];

    if (any) {
        ins.emplace_back(self.previous_output(), script_code, self.sequence());
    } else {
        for (auto const& input : inputs) {
            ins.emplace_back(input.previous_output(), script{}, 0);
        }

        ins[input_index].set_script(script_code);
        ins[input_index].set_sequence(self.sequence());
    }

    return signature_hash({tx.version(), tx.locktime(), std::move(ins), {}}, sighash_type);
}

static
std::pair<hash_digest, size_t> sign_single(transaction const& tx, uint32_t input_index, script const& script_code, uint8_t sighash_type) {
    input::list ins;
    auto const& inputs = tx.inputs();
    auto const any = (sighash_type & sighash_algorithm::anyone_can_pay) != 0;
    ins.reserve(any ? 1 : inputs.size());

    KTH_ASSERT(input_index < inputs.size());
    auto const& self = inputs[input_index];

    if (any) {
        ins.emplace_back(self.previous_output(), script_code, self.sequence());
    } else {
        for (auto const& input : inputs) {
            ins.emplace_back(input.previous_output(), script{}, 0);
        }

        ins[input_index].set_script(script_code);
        ins[input_index].set_sequence(self.sequence());
    }

    auto const& outputs = tx.outputs();
    output::list outs(input_index + 1);

    KTH_ASSERT(input_index < outputs.size());
    outs.back() = outputs[input_index];

    return signature_hash({tx.version(), tx.locktime(), std::move(ins),
                           std::move(outs)},
                          sighash_type);
}

static
std::pair<hash_digest, size_t> sign_all(transaction const& tx, uint32_t input_index, script const& script_code, uint8_t sighash_type) {
    input::list ins;
    auto const& inputs = tx.inputs();
    auto const any = (sighash_type & sighash_algorithm::anyone_can_pay) != 0;
    ins.reserve(any ? 1 : inputs.size());

    KTH_ASSERT(input_index < inputs.size());
    auto const& self = inputs[input_index];

    if (any) {
        ins.emplace_back(self.previous_output(), script_code, self.sequence());
    } else {
        for (auto const& input : inputs) {
            ins.emplace_back(input.previous_output(), script{},
                             input.sequence());
        }

        ins[input_index].set_script(script_code);
    }

    transaction out(tx.version(), tx.locktime(), input::list{}, tx.outputs());
    out.set_inputs(std::move(ins));
    return signature_hash(out, sighash_type);
}

static
script strip_code_seperators(script const& script_code) {
    operation::list ops;

    for (auto const& op : operations(script_code)) {
        if (op.code() != opcode::codeseparator) {
            ops.push_back(op);
        }
    }

    return script(std::move(ops));
}

// private/static
std::pair<hash_digest, size_t> script::generate_unversioned_signature_hash(transaction const& tx,
                                                        uint32_t input_index,
                                                        script const& script_code,
                                                        uint8_t sighash_type) {
    auto const sighash = to_sighash_enum(sighash_type);
    if (input_index >= tx.inputs().size() ||
        (input_index >= tx.outputs().size() && sighash == sighash_algorithm::single)) {
        //*********************************************************************
        // CONSENSUS: wacky satoshi behavior.
        //*********************************************************************
        return {one_hash, 0};
    }

    //*************************************************************************
    // CONSENSUS: more wacky satoshi behavior.
    //*************************************************************************
    auto const stripped = strip_code_seperators(script_code);

    switch (sighash) {
        case sighash_algorithm::none:
            return sign_none(tx, input_index, stripped, sighash_type);
        case sighash_algorithm::single:
            return sign_single(tx, input_index, stripped, sighash_type);
        default:
        case sighash_algorithm::all:
            return sign_all(tx, input_index, stripped, sighash_type);
    }
}

// Signing (version 0).
//-----------------------------------------------------------------------------

static
size_t preimage_size(size_t script_size) {
    return sizeof(uint32_t) + hash_size + hash_size + point::satoshi_fixed_size() + script_size + sizeof(uint64_t) + sizeof(uint32_t) + hash_size + sizeof(uint32_t) + sizeof(uint32_t);
}

// private/static
std::pair<hash_digest, size_t> script::generate_version_0_signature_hash(
    transaction const& tx,
    uint32_t input_index,
    script const& script_code,
    uint64_t value,
    uint8_t sighash_type,
    script_flags_t active_flags) {

    // Unlike unversioned algorithm this does not allow an invalid input index.
    KTH_ASSERT(input_index < tx.inputs().size());
    auto const& input = tx.inputs()[input_index];
    auto const& prevout = input.previous_output().validation.cache;
    KTH_ASSERT(prevout.is_valid());
    auto size = preimage_size(script_code.serialized_size(true));
    // preimage_size() covers the fixed 11 required fields only. Two
    // BCH-native additions to the sighash preimage (utxos hash after
    // field 2, and per-input token data after field 5) are opt-in per
    // sighash flag and consensus activation. When either fires the
    // buffer must grow to cover them or the writes below silently no-op
    // in release builds (KTH_ASSERT is compiled out), producing a
    // truncated sighash and NULLFAIL on every downstream signature.
#if defined(KTH_CURRENCY_BCH)
    auto const tokens_active = is_enabled(active_flags,
        domain::machine::script_flags::bch_tokens);
    if (tokens_active && (sighash_type & sighash_algorithm::utxos) != 0) {
        size += hash_size;
    }
    if (tokens_active && prevout.token_data().has_value()) {
        size += 1 /*prefix*/ + chain::token::encoding::serialized_size(prevout.token_data());
    }
#endif

    data_chunk data(size);
    byte_writer writer(data);

    auto const sighash = to_sighash_enum(sighash_type);
    auto const any = (sighash_type & sighash_algorithm::anyone_can_pay) != 0;

#if defined(KTH_CURRENCY_BCH)
    auto const single = sighash == sighash_algorithm::single ||
                        sighash == sighash_algorithm::forkid_single ||
                        sighash == sighash_algorithm::utxos_single;
    auto const all = sighash == sighash_algorithm::all ||
                     sighash == sighash_algorithm::forkid_all ||
                     sighash == sighash_algorithm::utxos_all;
    auto const utxos = (sighash_type & sighash_algorithm::utxos) != 0;
#else
    auto const single = sighash == sighash_algorithm::single;
    auto const all = sighash == sighash_algorithm::all;
#endif

    auto const single_out_hash = [&]() {
        auto const& out = tx.outputs()[input_index];
        return bitcoin_hash(to_data_chunk(out, true));
    };

    auto const write_ok = [&](expect<void> const& r) {
        KTH_CONTRACT(r.has_value());
    };

    // 1. transaction version (4-byte little endian).
    write_ok(writer.write_little_endian<uint32_t>(tx.version()));

    // 2. inpoints hash (32-byte hash).
    write_ok(writer.write_hash( ! any ? tx.inpoints_hash() : null_hash));

    // 3. Optional utxos hash (32-byte hash).
    if (is_enabled(active_flags, domain::machine::script_flags::bch_tokens) && utxos) {
        write_ok(writer.write_hash(tx.utxos_hash()));
    }

    // 4. sequences hash (32-byte hash).
    write_ok(writer.write_hash( ! any && all ? tx.sequences_hash() : null_hash));

    // 5. outpoint (32-byte hash + 4-byte little endian).
    write_ok(input.previous_output().to_data(writer, true));

    // 6. Optional token data (variable size).
    if (is_enabled(active_flags, domain::machine::script_flags::bch_tokens) && prevout.token_data().has_value()) {
        auto const& token_data = prevout.token_data().value();
        write_ok(writer.write_byte(chain::encoding::PREFIX_BYTE));
        write_ok(chain::token::encoding::to_data(writer, token_data));
    }

    // 7. script of the input (with prefix).
    write_ok(script_code.to_data(writer, true));

    // 8. value of the output spent by this input (8-byte little endian).
    write_ok(writer.write_little_endian<uint64_t>(value));

    // 9. sequence of the input (4-byte little endian).
    write_ok(writer.write_little_endian<uint32_t>(input.sequence()));

    // 10. outputs hash (32-byte hash).
    write_ok(writer.write_hash(all ?
        tx.outputs_hash() :
        (single && input_index < tx.outputs().size() ?
            single_out_hash() :
            null_hash)));

    // 11. transaction locktime (4-byte little endian).
    write_ok(writer.write_little_endian<uint32_t>(tx.locktime()));

    // 12. sighash type of the signature (4-byte [not 1] little endian).
    write_ok(writer.write_little_endian<uint32_t>(sighash_type));

    KTH_CONTRACT(writer.position() == data.size());
    return {bitcoin_hash(data), data.size()};
}

// Signing (common).
//-----------------------------------------------------------------------------

// static
std::pair<hash_digest, size_t> script::generate_signature_hash(
    transaction const& tx
    , uint32_t input_index
    , script const& script_code
    , uint8_t sighash_type
    , script_flags_t active_flags
#if ! defined(KTH_CURRENCY_BCH)
    , script_version version
#endif // ! KTH_CURRENCY_BCH
    , uint64_t value
) {

#if defined(KTH_CURRENCY_BCH)
    // Use BIP143-like sighash when FORKID bit is set and bch_uahf is active.
    // Otherwise fall back to classic sighash (pre-fork transactions).
    auto const has_forkid = (sighash_type & sighash_algorithm::forkid) != 0;
    if (has_forkid && is_enabled(active_flags, domain::machine::script_flags::bch_sighash_forkid)) {
        return generate_version_0_signature_hash(tx, input_index, script_code, value, sighash_type, active_flags);
    }
    return script::generate_unversioned_signature_hash(tx, input_index, script_code, sighash_type);
#else
    switch (version) {
        case script_version::unversioned:
            return generate_unversioned_signature_hash(tx, input_index, script_code, sighash_type);
        case script_version::zero:
            return generate_version_0_signature_hash(tx, input_index, script_code, value, sighash_type, active_flags);
        case script_version::reserved:
        default:
            KTH_ASSERT_MSG(false, "invalid script version");
            return {};
    }
#endif // KTH_CURRENCY_BCH
}

// static
std::pair<bool, size_t> script::check_signature(
    ec_signature const& signature
    , uint8_t sighash_type
    , data_chunk const& public_key
    , script const& script_code
    , transaction const& tx
    , uint32_t input_index
    , script_flags_t active_flags
#if ! defined(KTH_CURRENCY_BCH)
    , script_version version
#endif // ! KTH_CURRENCY_BCH
    , uint64_t value) {

    if (public_key.empty()) {
        return {false, 0};
    }

    auto const [sighash, size] = chain::script::generate_signature_hash(tx,
                                                                input_index,
                                                                script_code,
                                                                sighash_type,
                                                                active_flags,
#if ! defined(KTH_CURRENCY_BCH)
                                                                version,
#endif // ! KTH_CURRENCY_BCH
                                                                value);

    return { verify_signature(public_key, sighash, signature), size };
}

// static
std::expected<endorsement, std::error_code> script::create_endorsement(
    ec_secret const& secret,
    script const& prevout_script,
    transaction const& tx,
    uint32_t input_index,
    uint8_t sighash_type,
    script_flags_t active_flags,
#if ! defined(KTH_CURRENCY_BCH)
    script_version version /* = script_version::unversioned */,
#endif // ! KTH_CURRENCY_BCH
    uint64_t value /* = max_uint64 */,
    endorsement_type type /* = endorsement_type::ecdsa */) {

    auto const [sighash, size] = chain::script::generate_signature_hash(
        tx,
        input_index,
        prevout_script,
        sighash_type,
        active_flags,
#if ! defined(KTH_CURRENCY_BCH)
        version,
#endif // ! KTH_CURRENCY_BCH
        value
    );

    endorsement result;

    ec_signature signature;
    if (type == endorsement_type::ecdsa) {
        result.reserve(max_endorsement_size);
        if ( ! sign_ecdsa(signature, secret, sighash) || ! encode_signature(result, signature)) {
            return std::unexpected(error::invalid_signature_encoding);
        }

        result.push_back(sighash_type);
        result.shrink_to_fit();
    } else {
        if ( ! sign_schnorr(signature, secret, sighash)) {
            return std::unexpected(error::invalid_signature_encoding);
        }
        result.resize(schnorr_signature_size + 1);
        std::copy_n(signature.data(), schnorr_signature_size, result.begin());
        result.back() = sighash_type;
    }
    return result;
}

// Utilities (static).
//-----------------------------------------------------------------------------

bool script::is_push_only(operation::list const& ops) {
    auto const push = [](operation const& op) {
        return op.is_push();
    };

    return std::all_of(ops.begin(), ops.end(), push);
}

//*****************************************************************************
// CONSENSUS: this pattern is used to activate bip16 validation rules.
//*****************************************************************************
bool script::is_relaxed_push(operation::list const& ops) {
    auto const push = [&](operation const& op) {
        return op.is_relaxed_push();
    };

    return std::all_of(ops.begin(), ops.end(), push);
}

//*****************************************************************************
// CONSENSUS: BIP34 requires coinbase input script to begin with one byte that
// indicates the height size. This is inconsistent with an extreme future where
// the size byte overflows. However satoshi actually requires nominal encoding.
//*****************************************************************************
bool script::is_coinbase_pattern(operation::list const& ops, size_t height) {
    if (ops.empty()) return false;

    //Note(kth): Bitcoin core and derivatives do not follow the BIP34 specification.
    //  https://github.com/bitcoin/bitcoin/pull/14633
    if (height <= 16) {
        static constexpr auto op_1 = uint8_t(opcode::push_positive_1);
        auto const op_0 = uint8_t(ops[0].code());
        if (op_0 < op_1) return false;
        return height == op_0 - op_1 + 1;
    }

    if ( ! ops[0].is_nominal_push()) {
        return false;
    }
    auto num_exp = number::from_int(height);
    if ( ! num_exp) {
        return false;
    }
    auto const& num = *num_exp;
    return ops[0].data() == num.data();
}

// The satoshi client enables configurable data size for policy.
bool script::is_null_data_pattern(operation::list const& ops) {
    return ops.size() == 2 && ops[0].code() == opcode::return_ && ops[1].is_minimal_push() && ops[1].data().size() <= max_null_data_size;
}

// TODO(legacy): expand this to the 20 signature op_check_multisig limit.
bool script::is_pay_multisig_pattern(operation::list const& ops) {
    static constexpr auto op_1 = uint8_t(opcode::push_positive_1);
    static constexpr auto op_16 = uint8_t(opcode::push_positive_16);

    auto const op_count = ops.size();

    if (op_count < 4 || ops[op_count - 1].code() != opcode::checkmultisig) {
        return false;
    }

    auto const op_m = uint8_t(ops[0].code());
    auto const op_n = uint8_t(ops[op_count - 2].code());

    if (op_m < op_1 || op_m > op_n || op_n < op_1 || op_n > op_16) {
        return false;
    }

    auto const number = op_n - op_1 + 1u;
    auto const points = op_count - 3u;

    if (number != points) {
        return false;
    }

    for (auto op = ops.begin() + 1; op != ops.end() - 2; ++op) {
        if ( ! is_public_key(op->data())) {
            return false;
        }
    }

    return true;
}

bool script::is_pay_public_key_pattern(operation::list const& ops) {
    return ops.size() == 2 && is_public_key(ops[0].data()) && ops[1].code() == opcode::checksig;
}

bool script::is_pay_public_key_hash_pattern(operation::list const& ops) {
    return ops.size() == 5 &&
        ops[0].code() == opcode::dup &&
        ops[1].code() == opcode::hash160 &&
        ops[2].data().size() == short_hash_size &&
        ops[3].code() == opcode::equalverify &&
        ops[4].code() == opcode::checksig;
}

//*****************************************************************************
// CONSENSUS: this pattern is used to activate bip16 validation rules.
//*****************************************************************************
bool script::is_pay_script_hash_pattern(operation::list const& ops) {
    return ops.size() == 3 &&
        ops[0].code() == opcode::hash160 &&
        ops[1].code() == opcode::push_size_20 &&
        ops[2].code() == opcode::equal;
}

bool script::is_pay_script_hash_32_pattern(operation::list const& ops) {
    return ops.size() == 3 &&
        ops[0].code() == opcode::hash256 &&
        ops[1].code() == opcode::push_size_32 &&
        ops[2].code() == opcode::equal;
}

bool script::is_sign_multisig_pattern(operation::list const& ops) {
    return ops.size() >= 2 && ops[0].code() == opcode::push_size_0 && std::all_of(ops.begin() + 1, ops.end(), [](operation const& op) { return is_endorsement(op.data()); });
}

bool script::is_sign_public_key_pattern(operation::list const& ops) {
    return ops.size() == 1 && is_endorsement(ops[0].data());
}

//*****************************************************************************
// CONSENSUS: this pattern is used to activate bip141 validation rules.
//*****************************************************************************
bool script::is_sign_public_key_hash_pattern(operation::list const& ops) {
    return ops.size() == 2 && is_endorsement(ops[0].data()) && is_public_key(ops[1].data());
}

bool script::is_sign_script_hash_pattern(operation::list const& ops) {
    return !ops.empty() && is_push_only(ops) && !ops.back().data().empty();
}

operation::list script::to_null_data_pattern(byte_span data) {
    if (data.size() > max_null_data_size) {
        return {};
    }

    return operation::list{
        {opcode::return_},
        {to_chunk(data)}
    };
}

operation::list script::to_pay_public_key_pattern(byte_span point) {
    if ( ! is_public_key(point)) {
        return {};
    }

    return operation::list{
        {to_chunk(point)},
        {opcode::checksig}
    };
}

operation::list script::to_pay_public_key_hash_pattern(short_hash const& hash) {
    return operation::list{
        {opcode::dup},
        {opcode::hash160},
        {to_chunk(hash)},
        {opcode::equalverify},
        {opcode::checksig}};
}

operation::list script::to_pay_public_key_hash_pattern_unlocking(endorsement const& end, wallet::ec_public const& pubkey) {
    return operation::list {
        operation(opcode::push_size_0),
        operation(end),
        operation(pubkey.to_data())
    };
}

operation::list script::to_pay_public_key_hash_pattern_unlocking_placeholder(size_t endorsement_size, size_t pubkey_size) {
    data_chunk placeholder_signature(endorsement_size, 0);
    data_chunk placeholder_pubkey(pubkey_size, 0);
    return operation::list {
        operation(opcode::push_size_0),
        operation(std::move(placeholder_signature)),
        operation(std::move(placeholder_pubkey))
    };
}

operation::list script::to_pay_script_hash_pattern_unlocking_placeholder(size_t script_size, bool multisig) {
    data_chunk placeholder_script(script_size, 0);
    if (multisig) {
        // OP_0 dummy required by CHECKMULTISIG (historic off-by-one bug in Bitcoin).
        return operation::list {
            operation(opcode::push_size_0),
            operation(std::move(placeholder_script))
        };
    }
    return operation::list {
        operation(std::move(placeholder_script))
    };
}

operation::list script::to_pay_script_hash_pattern(short_hash const& hash) {
    return operation::list{
        {opcode::hash160},
        {to_chunk(hash)},
        {opcode::equal}};
}

operation::list script::to_pay_script_hash_32_pattern(hash_digest const& hash) {
    return operation::list{
        {opcode::hash256},
        {to_chunk(hash)},
        {opcode::equal}};
}

operation::list script::to_pay_multisig_pattern(uint8_t signatures, point_list const& points) {
    data_stack chunks;
    chunks.reserve(points.size());
    auto const conversion = [&chunks](ec_compressed const& point) {
        chunks.push_back(to_chunk(point));
    };

    // Operation ordering matters, don't use std::transform here.
    std::for_each(points.begin(), points.end(), conversion);
    return to_pay_multisig_pattern(signatures, chunks);
}

operation::list script::to_pay_multisig_pattern(uint8_t signatures, data_stack const& points) {
    static constexpr auto op_81 = uint8_t(opcode::push_positive_1);
    static constexpr auto op_96 = uint8_t(opcode::push_positive_16);
    static constexpr auto zero = op_81 - 1;
    static constexpr auto max = op_96 - zero;

    auto const m = signatures;
    auto const n = points.size();

    if (m < 1 || m > n || n < 1 || n > max) {
        return operation::list();
    }

    auto const op_m = static_cast<opcode>(m + zero);
    auto const op_n = static_cast<opcode>(points.size() + zero);

    operation::list ops;
    ops.reserve(points.size() + 3);
    ops.emplace_back(op_m);

    for (auto const& point : points) {
        if ( ! is_public_key(point)) {
            return {};
        }

        ops.emplace_back(point);
    }

    ops.emplace_back(op_n);
    ops.emplace_back(opcode::checkmultisig);
    return ops;
}

// Utilities (non-static).
//-----------------------------------------------------------------------------

// Count 1..16 multisig accurately for embedded (bip16) and witness (bip141).
inline size_t multisig_sigops(bool accurate, opcode code) {
    return accurate && operation::is_positive(code) ? operation::opcode_to_positive(code) : multisig_default_sigops;
}

script_pattern script::pattern() const {
    auto const input = output_pattern();
    return input == script_pattern::non_standard ? input_pattern() : input;
}

// Output patterns are mutually and input unambiguous.
script_pattern script::output_pattern() const {
    auto const ops = operations();

    if (is_pay_public_key_hash_pattern(ops)) {
        return script_pattern::pay_to_public_key_hash;
    }

    if (is_pay_script_hash_pattern(ops)) {
        return script_pattern::pay_to_script_hash;
    }

    if (is_pay_script_hash_32_pattern(ops)) {
        return script_pattern::pay_to_script_hash_32;
    }

    if (is_null_data_pattern(ops)) {
        return script_pattern::null_data;
    }

    if (is_pay_public_key_pattern(ops)) {
        return script_pattern::pay_to_public_key;
    }

    if (is_pay_multisig_pattern(ops)) {
        return script_pattern::pay_to_multisig;
    }

    return script_pattern::non_standard;
}

script_pattern script::output_pattern(script_flags_t flags) const {
    auto const base = output_pattern();
    if (base != script_pattern::non_standard) {
        return base;
    }
    // BCH 2026-May leibniz: anything that hasn't matched above and fits
    // within the P2S size bound counts as pay-to-script.
    if (script::is_enabled(flags, script_flags::bch_p2s) &&
        serialized_size(false) <= max_p2s_script_size) {
        return script_pattern::pay_to_script;
    }
    return script_pattern::non_standard;
}

script_pattern script::input_pattern() const {
    auto const ops = operations();

    if (is_sign_public_key_hash_pattern(ops)) {
        return script_pattern::sign_public_key_hash;
    }

    if (is_sign_script_hash_pattern(ops)) {
        return script_pattern::sign_script_hash;
    }

    if (is_sign_public_key_pattern(ops)) {
        return script_pattern::sign_public_key;
    }

    if (is_sign_multisig_pattern(ops)) {
        return script_pattern::sign_multisig;
    }

    return script_pattern::non_standard;
}

bool script::is_pay_to_script_hash(script_flags_t flags) const {
    return is_enabled(flags, script_flags::bip16_rule) &&
           is_pay_script_hash_pattern(operations());
}

bool script::is_pay_to_script_hash_32(script_flags_t flags) const {
#if defined(KTH_CURRENCY_BCH)
    return is_enabled(flags, script_flags::bch_p2sh_32) &&
           is_pay_script_hash_32_pattern(operations());
#else
    return false;
#endif
}

size_t script::sigops(bool accurate) const {
    size_t total = 0;
    auto preceding = opcode::reserved_255;

    for (auto const& op : operations()) {
        auto const code = op.code();

        if (code == opcode::checksig || code == opcode::checksigverify) {
            ++total;
        } else if (code == opcode::checkmultisig || code == opcode::checkmultisigverify) {
            total += multisig_sigops(accurate, preceding);
        }

        preceding = code;
    }

    return total;
}

bool script::is_unspendable() const {
    auto const ops = operations();
    return ( ! ops.empty() && ops[0].code() == opcode::return_) || serialized_size(false) > max_script_size;
}

// Validation.
//-----------------------------------------------------------------------------

code script::verify(transaction const& tx, uint32_t input_index, script_flags_t flags, script const& input_script, script const& prevout_script, uint64_t value) {
    return kth::domain::chain::verify(tx, input_index, flags, input_script, prevout_script, value);
}

code script::verify(transaction const& tx, uint32_t input, script_flags_t flags) {
    return kth::domain::chain::verify(tx, input, flags);
}

// Free functions.
//-----------------------------------------------------------------------------

operation::list operations(script const& s) {
    byte_reader reader(s.bytes());
    auto const size = s.bytes().size();

    operation::list res;
    // One operation per byte is the upper limit of operations.
    res.reserve(size);

    // ************************************************************************
    // CONSENSUS: In the case of a coinbase script we must parse the entire
    // script, beyond just the BIP34 requirements, so that sigops can be
    // calculated from the script. These are counted despite being irrelevant.
    // In this case an invalid script is parsed to the extent possible.
    // ************************************************************************

    while ( ! reader.is_exhausted()) {
        auto op = operation::from_data(reader);
        if ( ! op) {
            res.emplace_back(operation{});
            continue;
        }
        res.emplace_back(std::move(*op));
    }

    res.shrink_to_fit();
    return res;
}

operation first_operation(script const& s) {
    byte_reader reader(s.bytes());
    auto op = operation::from_data(reader);
    if ( ! op) {
        return operation{};
    }
    return *op;
}

} // namespace kth::domain::chain
