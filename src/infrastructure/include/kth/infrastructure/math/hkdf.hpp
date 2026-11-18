// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_MATH_HKDF_HPP
#define KTH_INFRASTRUCTURE_MATH_HKDF_HPP

#include <cstddef>
#include <optional>
#include <utility>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

/// HKDF with HMAC-SHA256 (RFC 5869).

/// The pseudorandom key: HMAC-SHA256 keyed with `salt` over `input_key`.
/// An empty salt stands for 32 zero bytes, as RFC 5869 says.
[[nodiscard]]
KI_API hash_digest hkdf_sha256_extract(byte_span salt, byte_span input_key);

/// `length` bytes of output keying material from `prk` and `info`. At most
/// 255 * 32 bytes can be derived; a longer request returns nullopt.
[[nodiscard]]
KI_API std::optional<data_chunk> hkdf_sha256_expand(hash_digest const& prk, byte_span info,
    size_t length);

/// Extract, then expand to two 32-byte keys with empty info: the HKDF of the
/// Noise framework (section 4.3), which derives a chaining key and a cipher key.
[[nodiscard]]
KI_API std::pair<hash_digest, hash_digest> hkdf_sha256_pair(hash_digest const& chaining_key,
    byte_span input_key);

} // namespace kth

#endif // KTH_INFRASTRUCTURE_MATH_HKDF_HPP
