// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_RESULT_CODE_HPP_
#define KTH_DATABASE_RESULT_CODE_HPP_

namespace kth::database {

enum class result_code {
    success = 0,
    success_duplicate_coinbase = 1,
    duplicated_key = 2,
    key_not_found = 3,
    db_empty = 4,
    no_data_to_prune = 5,
    db_corrupt = 6,
    prune_error = 7,
    other = 8
};

inline bool succeed(result_code code) {
    return code == result_code::success || code == result_code::success_duplicate_coinbase;
}

inline bool succeed_prune(result_code code) {
    return code == result_code::success || code == result_code::no_data_to_prune;
}

}  // namespace kth::database

#endif  // KTH_DATABASE_RESULT_CODE_HPP_
