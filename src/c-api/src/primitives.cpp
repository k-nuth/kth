// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/primitives.h>

#include <stdlib.h>

extern "C" {

void kth_core_destruct_array(uint8_t* arr) {
    free(arr);
}

void kth_core_destruct_string(char* str) {
    free(str);
}

} // extern "C"
