// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/executor/executor_info.hpp>

namespace kth::node {

using namespace kth::database;

std::string_view microarchitecture() {
    return KTH_MICROARCHITECTURE_STR;
}

std::string_view march_names() {
#if defined(KTH_MARCH_NAMES_FULL_STR)
    return KTH_MARCH_NAMES_FULL_STR;
#else
    return "unknown";
#endif
}

std::string_view currency_symbol() {
    return KTH_CURRENCY_SYMBOL_STR;
}

std::string_view currency() {
    return KTH_CURRENCY_STR;
}

std::string_view db_type(kth::database::db_mode_type db_mode) {
    std::string_view db_type_str;
    if (db_mode == db_mode_type::full) {
        return KTH_DB_TYPE_FULL;
    }
    if (db_mode == db_mode_type::blocks) {
        return KTH_DB_TYPE_BLOCKS;
    }
    // db_mode == db_mode_type::pruned
    return KTH_DB_TYPE_PRUNED;
}

uint32_t build_timestamp() {
#ifdef KTH_NODE_BUILD_TIMESTAMP
    return KTH_NODE_BUILD_TIMESTAMP;
#else
    return 0;
#endif
}

} // namespace kth::node
