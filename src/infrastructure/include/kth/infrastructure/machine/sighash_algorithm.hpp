// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_MACHINE_SIGHASH_ALGORITHM_HPP
#define KTH_INFRASTUCTURE_MACHINE_SIGHASH_ALGORITHM_HPP

#include <cstdint>

namespace kth::infrastructure::machine {


// Copied from Bitcoin Cash Node
// /** Signature hash types/flags */
// enum {
//     SIGHASH_ALL = 1,
//     SIGHASH_NONE = 2,
//     SIGHASH_SINGLE = 3,
//     SIGHASH_UTXOS = 0x20, ///< New in Upgrade9 (May 2023), must only be accepted if flags & SCRIPT_ENABLE_TOKENS
//     SIGHASH_FORKID = 0x40,
//     SIGHASH_ANYONECANPAY = 0x80,
// };

/// Signature hash types.
/// Comments from: bitcoin.org/en/developer-guide#standard-transactions
enum sighash_algorithm : uint32_t {
    /// The default, signs all the inputs and outputs, protecting everything
    /// except the signature scripts against modification.
    all = 0x01,

    /// Signs all of the inputs but none of the outputs, allowing anyone to
    /// change where the satoshis are going unless other signatures using
    /// other signature hash flags protect the outputs.
    none = 0x02,

    /// The only output signed is the one corresponding to this input (the
    /// output with the same output index number as this input), ensuring
    /// nobody can change your part of the transaction but allowing other
    /// signers to change their part of the transaction. The corresponding
    /// output must exist or the value '1' will be signed, breaking the
    /// security scheme. This input, as well as other inputs, are included
    /// in the signature. The sequence numbers of other inputs are not
    /// included in the signature, and can be updated.
    single = 0x03,



#if defined(KTH_CURRENCY_BCH)
    ///< New in Descartes/Upgrade9 (2023-May), must only be accepted if flags & SCRIPT_ENABLE_TOKENS
    utxos = 0x20,
    utxos_all = all | utxos,
    utxos_none = none | utxos,
    utxos_single = single | utxos,

    forkid = 0x40,
    forkid_all = all | forkid,
    forkid_none = none | forkid,
    forkid_single = single | forkid,
#endif

    /// The above types can be modified with this flag, creating three new
    /// combined types.
    anyone_can_pay = 0x80,

    /// Signs all of the outputs but only this one input, and it also allows
    /// anyone to add or remove other inputs, so anyone can contribute
    /// additional satoshis but they cannot change how many satoshis are
    /// sent nor where they go.
    anyone_can_pay_all = all | anyone_can_pay,

    /// Signs only this one input and allows anyone to add or remove other
    /// inputs or outputs, so anyone who gets a copy of this input can spend
    /// it however they'd like.
    anyone_can_pay_none = none | anyone_can_pay,

    /// Signs this one input and its corresponding output. Allows anyone to
    /// add or remove other inputs.
    anyone_can_pay_single = single | anyone_can_pay,

    /// Used to mask unused bits in the signature hash byte.
    mask = 0x1f
};

} // namespace kth::infrastructure::machine

#endif
