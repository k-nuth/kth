// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdio.h>

#include <kth/capi/chain/chain.h>
#include <kth/capi/chain/history_compact.h>
#include <kth/capi/chain/history_compact_list.h>
#include <kth/capi/chain/input.h>
#include <kth/capi/chain/input_list.h>
#include <kth/capi/chain/output.h>
#include <kth/capi/chain/output_list.h>
#include <kth/capi/chain/output_point.h>
#include <kth/capi/chain/script.h>
#include <kth/capi/chain/transaction.h>
#include <kth/capi/chain/transaction_list.h>
#include <kth/capi/node.h>
#include <kth/capi/hash_list.h>

// #include <kth/capi/helpers.hpp>
#include <kth/capi/string_list.h>
#include <kth/capi/wallet/payment_address.h>
#include <kth/capi/wallet/wallet.h>
#include <kth/capi/wallet/payment_address_list.h>
#include <kth/capi/wallet/hd_private.h>
#include <kth/capi/wallet/hd_public.h>
#include <kth/capi/wallet/elliptic_curve.h>
#include <kth/capi/wallet/ec_public.h>

void print_hex(uint8_t const* data, size_t n) {
    while (n != 0) {
        printf("%02x", *data);
        ++data;
        --n;
    }
    printf("\n");
}

#define MAINNET_P2KH 0x00
#define MAINNET_P2SH 0x05


int main(int argc, char* argv[]) {
    // m/44'/145'/0'
    char const* m44h145h0h_xpub_str = "xpub...";

    kth_hd_public_t m44h145h0h = kth_wallet_hd_public_construct_string(m44h145h0h_xpub_str);
    kth_hd_public_t m44h145h0h0 = kth_wallet_hd_public_derive_public(m44h145h0h, 0);

    printf("BIP44 Account Extended Public Key:  %s\n", kth_wallet_hd_public_encoded(m44h145h0h));
    printf("BIP32 Account Extended Public Key:  %s\n", kth_wallet_hd_public_encoded(m44h145h0h0));

    // print addresses
    for (size_t i = 0; i < 20; ++i) {
        kth_hd_public_t key = kth_wallet_hd_public_derive_public(m44h145h0h0, i);
        kth_ec_compressed_t point = kth_wallet_hd_public_point(key);
        kth_ec_public_t ecp = kth_wallet_ec_public_construct_from_point(&point, 1);
        kth_payment_address_t pa = kth_wallet_ec_public_to_payment_address(ecp, MAINNET_P2KH);
        printf("%s\n", kth_wallet_payment_address_encoded_cashaddr(pa, 0));
    }

    printf("\n");
}
