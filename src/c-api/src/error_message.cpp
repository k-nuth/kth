// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/error.h>
#include <kth/infrastructure/error.hpp>

extern "C" {

char const* kth_error_message(int32_t code) {
    switch (code) {
        case kth_ec_out_of_memory: return "out of memory";
        case kth_ec_unhandled_exception: return "unhandled exception";
        // The native table stores only complete NUL-terminated literals.
        default: return kth::error::message(code).data();
    }
}

} // extern "C"
