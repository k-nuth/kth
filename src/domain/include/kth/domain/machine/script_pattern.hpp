// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_SCRIPT_PATTERN_HPP
#define KTH_DOMAIN_MACHINE_SCRIPT_PATTERN_HPP

#include <string_view>

namespace kth::domain::machine {

/// Script patterns.
/// Comments from: bitcoin.org/en/developer-guide#signature-hash-types
enum class script_pattern {
    /// Null Data
    /// Pubkey Script: OP_RETURN <0 to 80 bytes of data> (formerly 40 bytes)
    /// Null data scripts cannot be spent, so there's no signature script.
    null_data,

    /// Pay to Multisig [BIP11]
    /// Pubkey script: <m> <A pubkey>[B pubkey][C pubkey...] <n> OP_CHECKMULTISIG
    /// Signature script: OP_0 <A sig>[B sig][C sig...]
    pay_to_multisig,

    /// Pay to Public Key (obsolete)
    pay_to_public_key,

    /// Pay to Public Key Hash [P2PKH]
    /// Pubkey script: OP_DUP OP_HASH160 <PubKeyHash> OP_EQUALVERIFY OP_CHECKSIG
    /// Signature script: <sig> <pubkey>
    pay_to_public_key_hash,

    /// Pay to Script Hash [P2SH/BIP16]
    /// The redeem script may be any pay type, but only multisig makes sense.
    /// Pubkey script: OP_HASH160 <Hash160(redeemScript)> OP_EQUAL
    /// Signature script: <sig>[sig][sig...] <redeemScript>
    pay_to_script_hash,

    /// Pay to Script Hash 32
    /// The redeem script may be any pay type, but only multisig makes sense.
    /// Pubkey script: OP_HASH256 <Hash256(redeemScript)> OP_EQUAL
    /// Signature script: <sig>[sig][sig...] <redeemScript>
    pay_to_script_hash_32,

    /// Sign Multisig script [BIP11]
    sign_multisig,

    /// Sign Public Key (obsolete)
    sign_public_key,

    /// Sign Public Key Hash [P2PKH]
    sign_public_key_hash,

    /// Sign Script Hash [P2SH/BIP16]
    sign_script_hash,

    /// Witness coinbase reserved value [BIP141].
    witness_reservation,

    /// The script may be valid but does not conform to the common templates.
    /// Such scripts are always accepted if they are mined into blocks, but
    /// transactions with uncommon scripts may not be forwarded by peers.
    non_standard
};

/// String representation of a script_pattern enumerator. Used by the C-API
/// (and any other consumer that needs a stable textual name) so the
/// enum-to-string mapping lives next to the enum definition itself instead
/// of being duplicated in wrappers.
///
/// TODO(C++26 reflection): replace the hand-written switch with a generic
/// enum_to_string<E> built on top of P2996 reflection (`std::meta::*`),
/// applied uniformly to every enum in the codebase.
constexpr
std::string_view to_string(script_pattern p) {
    switch (p) {
        case script_pattern::null_data:                 return "null_data";
        case script_pattern::pay_to_multisig:           return "pay_to_multisig";
        case script_pattern::pay_to_public_key:         return "pay_to_public_key";
        case script_pattern::pay_to_public_key_hash:    return "pay_to_public_key_hash";
        case script_pattern::pay_to_script_hash:        return "pay_to_script_hash";
        case script_pattern::pay_to_script_hash_32:     return "pay_to_script_hash_32";
        case script_pattern::sign_multisig:             return "sign_multisig";
        case script_pattern::sign_public_key:           return "sign_public_key";
        case script_pattern::sign_public_key_hash:      return "sign_public_key_hash";
        case script_pattern::sign_script_hash:          return "sign_script_hash";
        case script_pattern::witness_reservation:       return "witness_reservation";
        case script_pattern::non_standard:              return "non_standard";
    }
    return "non_standard";
}

} // namespace kth::domain::machine

#endif
