// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef KTH_DOMAIN_WALLET_TRANSACTION_FUNCTIONS_HPP
#define KTH_DOMAIN_WALLET_TRANSACTION_FUNCTIONS_HPP

#include <string>
#include <utility>
#include <vector>

#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/domain/chain/output.hpp>

namespace kth::domain::wallet {

using raw_output = std::pair<payment_address, uint64_t>;
using raw_output_list = std::vector<raw_output>;

using return1_t = std::pair<error::error_code_t, chain::transaction>;
using return2_t = std::pair<error::error_code_t, data_chunk>;

KD_API
return1_t tx_encode(chain::input_point::list const& outputs_to_spend,
                    raw_output_list const& destiny_and_amount,
                    std::vector<chain::output> const& extra_outputs,
                    uint32_t locktime = 0,
                    uint32_t tx_version = 1,
                    uint8_t script_version = 5);

KD_API
return1_t tx_encode(chain::input_point::list const& outputs_to_spend,
                    raw_output_list const& destiny_and_amount,
                    uint32_t locktime = 0,
                    uint32_t tx_version = 1,
                    uint8_t script_version = 5);

KD_API
return2_t input_signature_old(kth::ec_secret const& private_key,
                                chain::script const& output_script,
                                chain::transaction const& tx,
                                uint32_t index,
                                uint8_t sign_type = 0x01,
                                bool anyone_can_pay = false);

KD_API
return2_t input_signature_btc(kth::ec_secret const& private_key,
                            chain::script const& output_script,
                            chain::transaction const& tx,
                            uint64_t amount,
                            uint32_t index,
                            uint8_t sign_type = 0x01,
                            bool anyone_can_pay = false);

KD_API
return2_t input_signature_bch(kth::ec_secret const& private_key,
                            chain::script const& output_script,
                            chain::transaction const& tx,
                            uint64_t amount,
                            uint32_t index,
                            uint8_t sign_type = 0x01,
                            bool anyone_can_pay = false);

KD_API
return1_t input_set(chain::script const& script,
                    chain::transaction const& raw_tx,
                    uint32_t index = 0);

KD_API
return1_t input_set(data_chunk const& signature,
                    wallet::ec_public const& public_key,
                    chain::transaction const& raw_tx,
                    uint32_t index = 0);

}  // namespace kth::domain::wallet

#endif  //KTH_DOMAIN_WALLET_TRANSACTION_FUNCTIONS_HPP