// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_BYTE_CODE_PATTERN_HPP
#define KTH_DOMAIN_CHAIN_BYTE_CODE_PATTERN_HPP

#include <vector>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::chain {

// A script's byte code pattern, the "byteCodePattern" object getblock gives
// with `patterns`: the script with each run of data pushes replaced by one
// number, the count of the run, and the pushed data in order. A push of a
// number (OP_1NEGATE, OP_1 to OP_16) counts as a data push of its value. A
// script that ends in the middle of a push keeps the pattern up to there, and
// its last data item is the rest of the script, from the byte before the point
// where reading stopped.
struct byte_code_pattern {
    data_chunk pattern;
    std::vector<data_chunk> data;
    bool error = false;
};

[[nodiscard]]
KD_API byte_code_pattern make_byte_code_pattern(byte_span script);

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_BYTE_CODE_PATTERN_HPP
