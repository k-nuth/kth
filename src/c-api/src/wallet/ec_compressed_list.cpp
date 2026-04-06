// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/ec_compressed_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/wallet/conversions.hpp>

using ec_compressed_vec = std::vector<ec_compressed_cpp_t>;

// Global converters
ec_compressed_vec const& kth_wallet_ec_compressed_list_const_cpp(kth_ec_compressed_list_t l) {
    return *static_cast<ec_compressed_vec const*>(l);
}

ec_compressed_vec& kth_wallet_ec_compressed_list_cpp(kth_ec_compressed_list_t l) {
    return *static_cast<ec_compressed_vec*>(l);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_ec_compressed_list_t kth_wallet_ec_compressed_list_construct_default() {
    return new ec_compressed_vec();
}

void kth_wallet_ec_compressed_list_push_back(kth_ec_compressed_list_t l, kth_ec_compressed_t e) {
    kth_wallet_ec_compressed_list_cpp(l).push_back(detail::from_ec_compressed_t(e));
}

void kth_wallet_ec_compressed_list_destruct(kth_ec_compressed_list_t l) {
    delete &kth_wallet_ec_compressed_list_cpp(l);
}

kth_size_t kth_wallet_ec_compressed_list_count(kth_ec_compressed_list_t l) {
    return kth_wallet_ec_compressed_list_const_cpp(l).size();
}

kth_ec_compressed_t kth_wallet_ec_compressed_list_nth(kth_ec_compressed_list_t l, kth_size_t n) {
    auto const& x = kth_wallet_ec_compressed_list_cpp(l)[n];
    return detail::to_ec_compressed_t(x);
}

void kth_wallet_ec_compressed_list_nth_out(kth_ec_compressed_list_t l, kth_size_t n, kth_ec_compressed_t* out_elem) {
    auto const& x = kth_wallet_ec_compressed_list_cpp(l)[n];
    std::copy_n(x.begin(), x.size(), static_cast<uint8_t*>(out_elem->data));
}

} // extern "C"
