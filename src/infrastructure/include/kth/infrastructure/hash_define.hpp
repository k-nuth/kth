// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_HASH_DEFINE_HPP
#define KTH_INFRASTRUCTURE_HASH_DEFINE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include <boost/functional/hash_fwd.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/endian.hpp>

namespace kth {

// Common bitcoin hash container sizes.
static constexpr size_t hash_size = 32;
static constexpr size_t half_hash_size = hash_size / 2;
static constexpr size_t quarter_hash_size = half_hash_size / 2;
static constexpr size_t long_hash_size = 2 * hash_size;
static constexpr size_t short_hash_size = 20;
static constexpr size_t mini_hash_size = 6;

// Common bitcoin hash containers.
using hash_digest = byte_array<hash_size>;
using half_hash = byte_array<half_hash_size>;
using quarter_hash = byte_array<quarter_hash_size>;
using long_hash = byte_array<long_hash_size>;
using short_hash = byte_array<short_hash_size>;
using mini_hash = byte_array<mini_hash_size>;

// Lists of common bitcoin hashes.
using hash_list = std::vector<hash_digest>;
using half_hash_list = std::vector<half_hash>;
using quarter_hash_list = std::vector<quarter_hash>;
using long_hash_list = std::vector<long_hash>;
using short_hash_list = std::vector<short_hash>;
using mini_hash_list = std::vector<mini_hash>;

// Null-valued common bitcoin hashes.
constexpr
hash_digest null_hash {{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

} // namespace kth

#endif
