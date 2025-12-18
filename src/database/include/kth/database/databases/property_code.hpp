// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_PROPERTY_CODE_HPP_
#define KTH_DATABASE_PROPERTY_CODE_HPP_

#include <algorithm>
#include <cctype>
#include <istream>

#include <boost/program_options.hpp>

namespace kth::database {

// TODO(fernando): rename

enum class property_code {
    db_mode = 0,
    last_header_height = 1,
    last_block_height = 2,
};

enum class db_mode_type {
    pruned,
    blocks,
    full,
};

inline
std::istream& operator>> (std::istream &in, db_mode_type& db_mode) {
    using namespace boost::program_options;

    std::string mode_str;
    in >> mode_str;

    std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(),
        [](unsigned char c){ return std::toupper(c); });

    if (mode_str == "PRUNED") {
        db_mode = db_mode_type::pruned;
    }
    else if (mode_str == "BLOCKS") {
        db_mode = db_mode_type::blocks;
    }
    else if (mode_str == "FULL") {
        db_mode = db_mode_type::full;
    }
    else {
        throw validation_error(validation_error::invalid_option_value);
    }

    return in;
}

} // namespace kth::database

#endif // KTH_DATABASE_PROPERTY_CODE_HPP_
