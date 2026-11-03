// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <kth/capi/error.h>
#include <kth/capi/primitives.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#error "The C API C targets require C23"
#endif

// C23 keyword spelling, without relying on the pre-C23 assert.h macro.
static_assert(kth_ec_success == 0);
static_assert(sizeof(kth_bool_t) == sizeof(int), "C API boolean ABI");
static_assert(sizeof(kth_size_t) == sizeof(size_t), "C API size ABI");
