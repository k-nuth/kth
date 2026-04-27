// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>

#include <kth/capi/wallet/cashaddr.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/cashaddr.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Static utilities

char* kth_wallet_cashaddr_encode(char const* prefix, uint8_t const* payload, kth_size_t n) {
    KTH_PRECONDITION(prefix != nullptr);
    KTH_PRECONDITION(payload != nullptr || n == 0);
    auto const prefix_cpp = std::string_view(prefix);
    auto const payload_cpp = n != 0 ? kth::data_chunk(payload, payload + n) : kth::data_chunk{};
    auto const s = kth::domain::wallet::cashaddr::encode(prefix_cpp, payload_cpp);
    return kth::create_c_str(s);
}

char* kth_wallet_cashaddr_decode(char const* str, char const* default_prefix, KTH_OUT_OWNED uint8_t** out_payload, kth_size_t* out_payload_size) {
    KTH_PRECONDITION(str != nullptr);
    KTH_PRECONDITION(default_prefix != nullptr);
    KTH_PRECONDITION(out_payload != nullptr);
    KTH_PRECONDITION(out_payload_size != nullptr);
    auto const str_cpp = std::string(str);
    auto const default_prefix_cpp = std::string(default_prefix);
    auto const pair_cpp = kth::domain::wallet::cashaddr::decode(str_cpp, default_prefix_cpp);
    *out_payload = kth::create_c_array(pair_cpp.second, *out_payload_size);
    return kth::create_c_str(pair_cpp.first);
}

} // extern "C"
