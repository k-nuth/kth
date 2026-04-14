// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/script.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/script.hpp>

// Conversion functions
kth::domain::chain::script& kth_chain_script_mut_cpp(kth_script_mut_t o) {
    return *static_cast<kth::domain::chain::script*>(o);
}
kth::domain::chain::script const& kth_chain_script_const_cpp(kth_script_const_t o) {
    return *static_cast<kth::domain::chain::script const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_script_mut_t kth_chain_script_construct_default(void) {
    return new kth::domain::chain::script();
}

kth_error_code_t kth_chain_script_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_script_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = kth::domain::chain::script::from_data(data_cpp, wire_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_script_mut_t kth_chain_script_construct_from_operations(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::make_leaked_if_valid(kth::domain::chain::script(ops_cpp));
}

kth_script_mut_t kth_chain_script_construct_from_encoded_prefix(uint8_t const* encoded, kth_size_t n, kth_bool_t prefix) {
    KTH_PRECONDITION(encoded != nullptr || n == 0);
    auto const encoded_cpp = n != 0 ? kth::data_chunk(encoded, encoded + n) : kth::data_chunk{};
    auto const prefix_cpp = kth::int_to_bool(prefix);
    return kth::make_leaked_if_valid(kth::domain::chain::script(encoded_cpp, prefix_cpp));
}


// Destructor

void kth_chain_script_destruct(kth_script_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_script_mut_cpp(self);
}


// Copy

kth_script_mut_t kth_chain_script_copy(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::script(kth_chain_script_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_script_equals(kth_script_const_t self, kth_script_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self) == kth_chain_script_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_script_to_data(kth_script_const_t self, kth_bool_t prefix, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const prefix_cpp = kth::int_to_bool(prefix);
    auto const data = kth_chain_script_const_cpp(self).to_data(prefix_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_script_serialized_size(kth_script_const_t self, kth_bool_t prefix) {
    KTH_PRECONDITION(self != nullptr);
    auto const prefix_cpp = kth::int_to_bool(prefix);
    return kth_chain_script_const_cpp(self).serialized_size(prefix_cpp);
}


// Getters

kth_bool_t kth_chain_script_empty(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self).empty());
}

kth_size_t kth_chain_script_size(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_script_const_cpp(self).size();
}

kth_operation_const_t kth_chain_script_front(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION( ! kth_chain_script_const_cpp(self).empty());
    return &(kth_chain_script_const_cpp(self).front());
}

kth_operation_const_t kth_chain_script_back(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION( ! kth_chain_script_const_cpp(self).empty());
    return &(kth_chain_script_const_cpp(self).back());
}

kth_operation_list_const_t kth_chain_script_operations(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_script_const_cpp(self).operations());
}

kth_operation_mut_t kth_chain_script_first_operation(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth_chain_script_const_cpp(self).first_operation());
}

kth_script_pattern_t kth_chain_script_pattern(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_script_pattern_t>(kth_chain_script_const_cpp(self).pattern());
}

kth_script_pattern_t kth_chain_script_output_pattern(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_script_pattern_t>(kth_chain_script_const_cpp(self).output_pattern());
}

kth_script_pattern_t kth_chain_script_input_pattern(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return static_cast<kth_script_pattern_t>(kth_chain_script_const_cpp(self).input_pattern());
}

uint8_t* kth_chain_script_bytes(kth_script_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth_chain_script_const_cpp(self).bytes();
    return kth::create_c_array(data, *out_size);
}


// Predicates

kth_bool_t kth_chain_script_is_valid_operations(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self).is_valid_operations());
}

kth_bool_t kth_chain_script_is_push_only(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_push_only(ops_cpp));
}

kth_bool_t kth_chain_script_is_relaxed_push(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_relaxed_push(ops_cpp));
}

kth_bool_t kth_chain_script_is_coinbase_pattern(kth_operation_list_const_t ops, kth_size_t height) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    auto const height_cpp = static_cast<size_t>(height);
    return kth::bool_to_int(kth::domain::chain::script::is_coinbase_pattern(ops_cpp, height_cpp));
}

kth_bool_t kth_chain_script_is_null_data_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_null_data_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_pay_multisig_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_pay_multisig_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_pay_public_key_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_pay_public_key_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_pay_public_key_hash_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_pay_public_key_hash_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_pay_script_hash_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_pay_script_hash_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_pay_script_hash_32_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_pay_script_hash_32_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_sign_multisig_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_sign_multisig_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_sign_public_key_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_sign_public_key_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_sign_public_key_hash_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_sign_public_key_hash_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_sign_script_hash_pattern(kth_operation_list_const_t ops) {
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    return kth::bool_to_int(kth::domain::chain::script::is_sign_script_hash_pattern(ops_cpp));
}

kth_bool_t kth_chain_script_is_unspendable(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self).is_unspendable());
}

kth_bool_t kth_chain_script_is_pay_to_script_hash(kth_script_const_t self, kth_script_flags_t flags) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self).is_pay_to_script_hash(flags));
}

kth_bool_t kth_chain_script_is_pay_to_script_hash_32(kth_script_const_t self, kth_script_flags_t flags) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self).is_pay_to_script_hash_32(flags));
}

kth_bool_t kth_chain_script_is_valid(kth_script_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_script_const_cpp(self).is_valid());
}

kth_bool_t kth_chain_script_is_enabled(kth_script_flags_t active_flags, kth_script_flags_t fork) {
    return kth::bool_to_int(kth::domain::chain::script::is_enabled(active_flags, fork));
}


// Operations

void kth_chain_script_from_operations(kth_script_mut_t self, kth_operation_list_const_t ops) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(ops != nullptr);
    auto const& ops_cpp = kth_chain_operation_list_const_cpp(ops);
    kth_chain_script_mut_cpp(self).from_operations(ops_cpp);
}

kth_bool_t kth_chain_script_from_string(kth_script_mut_t self, char const* mnemonic) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(mnemonic != nullptr);
    auto const mnemonic_cpp = std::string(mnemonic);
    return kth::bool_to_int(kth_chain_script_mut_cpp(self).from_string(mnemonic_cpp));
}

char* kth_chain_script_to_string(kth_script_const_t self, kth_script_flags_t active_flags) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth_chain_script_const_cpp(self).to_string(active_flags);
    return kth::create_c_str(s);
}

void kth_chain_script_clear(kth_script_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_script_mut_cpp(self).clear();
}

kth_operation_const_t kth_chain_script_at(kth_script_const_t self, kth_size_t index) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(index < kth_chain_script_const_cpp(self).size());
    auto const index_cpp = static_cast<size_t>(index);
    return &(kth_chain_script_const_cpp(self).operator[](index_cpp));
}

kth_size_t kth_chain_script_sigops(kth_script_const_t self, kth_bool_t accurate) {
    KTH_PRECONDITION(self != nullptr);
    auto const accurate_cpp = kth::int_to_bool(accurate);
    return kth_chain_script_const_cpp(self).sigops(accurate_cpp);
}

void kth_chain_script_reset(kth_script_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_script_mut_cpp(self).reset();
}


// Static utilities

kth_error_code_t kth_chain_script_from_data_with_size(uint8_t const* data, kth_size_t n, kth_size_t size, KTH_OUT_OWNED kth_script_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto const size_cpp = static_cast<size_t>(size);
    auto result = kth::domain::chain::script::from_data_with_size(data_cpp, size_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_hash_t kth_chain_script_generate_signature_hash(kth_transaction_const_t tx, uint32_t input_index, kth_script_const_t script_code, uint8_t sighash_type, kth_script_flags_t active_flags, uint64_t value, kth_size_t* out_size) {
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(script_code != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    auto const& script_code_cpp = kth_chain_script_const_cpp(script_code);
    auto const pair_cpp = kth::domain::chain::script::generate_signature_hash(tx_cpp, input_index, script_code_cpp, sighash_type, active_flags, value);
    *out_size = pair_cpp.second;
    return kth::to_hash_t(pair_cpp.first);
}

kth_bool_t kth_chain_script_check_signature(kth_longhash_t signature, uint8_t sighash_type, uint8_t const* public_key, kth_size_t n, kth_script_const_t script_code, kth_transaction_const_t tx, uint32_t input_index, kth_script_flags_t active_flags, uint64_t value, kth_size_t* out_size) {
    KTH_PRECONDITION(public_key != nullptr || n == 0);
    KTH_PRECONDITION(script_code != nullptr);
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const signature_cpp = kth::long_hash_to_cpp(signature.hash);
    auto const public_key_cpp = n != 0 ? kth::data_chunk(public_key, public_key + n) : kth::data_chunk{};
    auto const& script_code_cpp = kth_chain_script_const_cpp(script_code);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    auto const pair_cpp = kth::domain::chain::script::check_signature(signature_cpp, sighash_type, public_key_cpp, script_code_cpp, tx_cpp, input_index, active_flags, value);
    *out_size = pair_cpp.second;
    return kth::bool_to_int(pair_cpp.first);
}

kth_bool_t kth_chain_script_check_signature_unsafe(uint8_t const* signature, uint8_t sighash_type, uint8_t const* public_key, kth_size_t n, kth_script_const_t script_code, kth_transaction_const_t tx, uint32_t input_index, kth_script_flags_t active_flags, uint64_t value, kth_size_t* out_size) {
    KTH_PRECONDITION(signature != nullptr);
    KTH_PRECONDITION(public_key != nullptr || n == 0);
    KTH_PRECONDITION(script_code != nullptr);
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const signature_cpp = kth::long_hash_to_cpp(signature);
    auto const public_key_cpp = n != 0 ? kth::data_chunk(public_key, public_key + n) : kth::data_chunk{};
    auto const& script_code_cpp = kth_chain_script_const_cpp(script_code);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    auto const pair_cpp = kth::domain::chain::script::check_signature(signature_cpp, sighash_type, public_key_cpp, script_code_cpp, tx_cpp, input_index, active_flags, value);
    *out_size = pair_cpp.second;
    return kth::bool_to_int(pair_cpp.first);
}

kth_error_code_t kth_chain_script_create_endorsement(kth_hash_t secret, kth_script_const_t prevout_script, kth_transaction_const_t tx, uint32_t input_index, uint8_t sighash_type, kth_script_flags_t active_flags, uint64_t value, kth_endorsement_type_t type, KTH_OUT_OWNED uint8_t** out, kth_size_t* out_size) {
    KTH_PRECONDITION(prevout_script != nullptr);
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const secret_cpp = kth::hash_to_cpp(secret.hash);
    auto const& prevout_script_cpp = kth_chain_script_const_cpp(prevout_script);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    auto const type_cpp = static_cast<kth::domain::chain::endorsement_type>(type);
    auto const result = kth::domain::chain::script::create_endorsement(secret_cpp, prevout_script_cpp, tx_cpp, input_index, sighash_type, active_flags, value, type_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::create_c_array(*result, *out_size);
    return kth_ec_success;
}

kth_error_code_t kth_chain_script_create_endorsement_unsafe(uint8_t const* secret, kth_script_const_t prevout_script, kth_transaction_const_t tx, uint32_t input_index, uint8_t sighash_type, kth_script_flags_t active_flags, uint64_t value, kth_endorsement_type_t type, KTH_OUT_OWNED uint8_t** out, kth_size_t* out_size) {
    KTH_PRECONDITION(secret != nullptr);
    KTH_PRECONDITION(prevout_script != nullptr);
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const secret_cpp = kth::hash_to_cpp(secret);
    auto const& prevout_script_cpp = kth_chain_script_const_cpp(prevout_script);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    auto const type_cpp = static_cast<kth::domain::chain::endorsement_type>(type);
    auto const result = kth::domain::chain::script::create_endorsement(secret_cpp, prevout_script_cpp, tx_cpp, input_index, sighash_type, active_flags, value, type_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::create_c_array(*result, *out_size);
    return kth_ec_success;
}

kth_operation_list_mut_t kth_chain_script_to_null_data_pattern(uint8_t const* data, kth_size_t n) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = kth::byte_span(data, static_cast<size_t>(n));
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_null_data_pattern(data_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_public_key_pattern(uint8_t const* point, kth_size_t n) {
    KTH_PRECONDITION(point != nullptr || n == 0);
    auto const point_cpp = kth::byte_span(point, static_cast<size_t>(n));
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_public_key_pattern(point_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_public_key_hash_pattern(kth_shorthash_t hash) {
    auto const hash_cpp = kth::short_hash_to_cpp(hash.hash);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_public_key_hash_pattern(hash_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_public_key_hash_pattern_unsafe(uint8_t const* hash) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::short_hash_to_cpp(hash);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_public_key_hash_pattern(hash_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_public_key_hash_pattern_unlocking(uint8_t const* end, kth_size_t n, kth_ec_public_const_t pubkey) {
    KTH_PRECONDITION(end != nullptr || n == 0);
    KTH_PRECONDITION(pubkey != nullptr);
    auto const end_cpp = n != 0 ? kth::data_chunk(end, end + n) : kth::data_chunk{};
    auto const& pubkey_cpp = kth_wallet_ec_public_const_cpp(pubkey);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_public_key_hash_pattern_unlocking(end_cpp, pubkey_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_public_key_hash_pattern_unlocking_placeholder(kth_size_t endorsement_size, kth_size_t pubkey_size) {
    auto const endorsement_size_cpp = static_cast<size_t>(endorsement_size);
    auto const pubkey_size_cpp = static_cast<size_t>(pubkey_size);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_public_key_hash_pattern_unlocking_placeholder(endorsement_size_cpp, pubkey_size_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_pattern_unlocking_placeholder(kth_size_t script_size, kth_bool_t multisig) {
    auto const script_size_cpp = static_cast<size_t>(script_size);
    auto const multisig_cpp = kth::int_to_bool(multisig);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_script_hash_pattern_unlocking_placeholder(script_size_cpp, multisig_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_pattern(kth_shorthash_t hash) {
    auto const hash_cpp = kth::short_hash_to_cpp(hash.hash);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_script_hash_pattern(hash_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_pattern_unsafe(uint8_t const* hash) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::short_hash_to_cpp(hash);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_script_hash_pattern(hash_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_32_pattern(kth_hash_t hash) {
    auto const hash_cpp = kth::hash_to_cpp(hash.hash);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_script_hash_32_pattern(hash_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_script_hash_32_pattern_unsafe(uint8_t const* hash) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_script_hash_32_pattern(hash_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_multisig_pattern_ec_compressed_list(uint8_t signatures, kth_ec_compressed_list_const_t points) {
    KTH_PRECONDITION(points != nullptr);
    auto const& points_cpp = kth_wallet_ec_compressed_list_const_cpp(points);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_multisig_pattern(signatures, points_cpp));
}

kth_operation_list_mut_t kth_chain_script_to_pay_multisig_pattern_data_stack(uint8_t signatures, kth_data_stack_const_t points) {
    KTH_PRECONDITION(points != nullptr);
    auto const& points_cpp = kth_chain_data_stack_const_cpp(points);
    return new std::vector<kth::domain::machine::operation>(kth::domain::chain::script::to_pay_multisig_pattern(signatures, points_cpp));
}

kth_error_code_t kth_chain_script_verify(kth_transaction_const_t tx, uint32_t input_index, kth_script_flags_t flags, kth_script_const_t input_script, kth_script_const_t prevout_script, uint64_t value) {
    KTH_PRECONDITION(tx != nullptr);
    KTH_PRECONDITION(input_script != nullptr);
    KTH_PRECONDITION(prevout_script != nullptr);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    auto const& input_script_cpp = kth_chain_script_const_cpp(input_script);
    auto const& prevout_script_cpp = kth_chain_script_const_cpp(prevout_script);
    return static_cast<kth_error_code_t>((kth::domain::chain::script::verify(tx_cpp, input_index, flags, input_script_cpp, prevout_script_cpp, value)).value());
}

kth_error_code_t kth_chain_script_verify_simple(kth_transaction_const_t tx, uint32_t input, kth_script_flags_t flags) {
    KTH_PRECONDITION(tx != nullptr);
    auto const& tx_cpp = kth_chain_transaction_const_cpp(tx);
    return static_cast<kth_error_code_t>((kth::domain::chain::script::verify(tx_cpp, input, flags)).value());
}

} // extern "C"
