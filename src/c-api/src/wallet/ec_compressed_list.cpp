// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/ec_compressed_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/capi/wallet/conversions.hpp>

// ---------------------------------------------------------------------------
extern "C" {

kth_ec_compressed_list_t kth_wallet_ec_compressed_list_construct_default() {
    return new std::vector<ec_compressed_cpp_t>();
}

void kth_wallet_ec_compressed_list_push_back(kth_ec_compressed_list_t l, kth_ec_compressed_t e) {
    kth::cpp_ref<std::vector<ec_compressed_cpp_t>>(l).push_back(detail::from_ec_compressed_t(e));
}

void kth_wallet_ec_compressed_list_destruct(kth_ec_compressed_list_t l) {
    delete &kth::cpp_ref<std::vector<ec_compressed_cpp_t>>(l);
}

kth_size_t kth_wallet_ec_compressed_list_count(kth_ec_compressed_list_t l) {
    return kth::cpp_ref<std::vector<ec_compressed_cpp_t>>(l).size();
}

kth_ec_compressed_t kth_wallet_ec_compressed_list_nth(kth_ec_compressed_list_t l, kth_size_t n) {
    auto const& x = kth::cpp_ref<std::vector<ec_compressed_cpp_t>>(l)[n];
    return detail::to_ec_compressed_t(x);
}

void kth_wallet_ec_compressed_list_nth_out(kth_ec_compressed_list_t l, kth_size_t n, kth_ec_compressed_t* out_elem) {
    auto const& x = kth::cpp_ref<std::vector<ec_compressed_cpp_t>>(l)[n];
    std::copy_n(x.begin(), x.size(), static_cast<uint8_t*>(out_elem->data));
}

} // extern "C"
