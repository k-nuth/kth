// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTUCTURE_BASE_85_HPP
#define KTH_INFRASTUCTURE_BASE_85_HPP

#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

/**
 * Encode data as base85 (Z85).
 * @return false if the input is not of base85 size (% 4).
 */
KI_API bool encode_base85(std::string& out, byte_span in);

/**
 * Attempt to decode base85 (Z85) data.
 * @return false if the input contains non-base85 characters or length (% 5).
 */
KI_API bool decode_base85(data_chunk& out, std::string_view in);

} // namespace kth

#endif
