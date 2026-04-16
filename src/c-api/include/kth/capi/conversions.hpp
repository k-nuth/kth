// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CONVERSIONS_HPP_
#define KTH_CAPI_CONVERSIONS_HPP_

// Umbrella header for the C-API bindings. It used to carry a forest
// of per-type `kth_<ns>_<name>_{const,mut}_cpp` forward declarations;
// those were retired in favour of the generic `kth::cpp_ref<T>(h)` /
// `kth::optional_cpp_ref<T>(h)` helpers in `helpers.hpp`. What's left
// here is an aggregate of the domain headers that binding TUs need so
// callers keep getting the expected type declarations from a single
// include point.

#include <array>
#include <cstdint>
#include <vector>

#include <kth/capi/primitives.h>
#include <kth/capi/wallet/primitives.h>

#include <kth/infrastructure/utility/binary.hpp>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/stealth.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/double_spend_proof.hpp>
#include <kth/domain/message/get_blocks.hpp>
#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/message/prefilled_transaction.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/domain/wallet/hd_private.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/wallet_manager.hpp>

// #ifndef __EMSCRIPTEN__
#include <kth/blockchain/interface/safe_chain.hpp>
// #endif

// Element type for the hand-written `kth_wallet_ec_compressed_list`
// binding. The list stores the raw 33-byte EC compressed public keys
// by value, not wrapped `kth::domain::wallet::ec_compressed` objects.
using ec_compressed_cpp_t = std::array<uint8_t, KTH_EC_COMPRESSED_SIZE>;

#endif /* KTH_CAPI_CONVERSIONS_HPP_ */
