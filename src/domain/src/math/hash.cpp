// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/hash.hpp>

// #include <algorithm>
// #include <cstddef>
// #include <cstdint>
// #include <errno.h>
// #include <new>
// #include <stdexcept>
// #include "../math/external/crypto_scrypt.h"
// #include "../math/external/hmac_sha256.h"
// #include "../math/external/hmac_sha512.h"
// #include "../math/external/pkcs5_pbkdf2.h"
// #include "../math/external/ripemd160.h"
// #include "../math/external/sha1.h"
// #include "../math/external/sha256.h"
// #include "../math/external/sha512.h"

#if defined(KTH_CURRENCY_LTC)
#include "../math/external/scrypt.h"
#endif  //KTH_CURRENCY_LTC

namespace kth {

#if defined(KTH_CURRENCY_LTC)
hash_digest litecoin_hash(byte_span data) {
    hash_digest hash;
    scrypt_1024_1_1_256(reinterpret_cast<char const*>(data.data()),
                        reinterpret_cast<char*>(hash.data()));
    return hash;
}
#endif  //KTH_CURRENCY_LTC

} // namespace kth
