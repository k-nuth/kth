// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_EXE_EXECUTOR_INFO_HPP_
#define KTH_NODE_EXE_EXECUTOR_INFO_HPP_

#include <string_view>

#include <kth/database/databases/property_code.hpp>

namespace kth::node {

std::string_view microarchitecture();
std::string_view march_names();
std::string_view currency_symbol();
std::string_view currency();
std::string_view db_type(kth::database::db_mode_type db_mode);
uint32_t build_timestamp();

} // namespace kth::node

#define KTH_DB_TYPE_FULL "full-indexed"
#define KTH_DB_TYPE_BLOCKS "blocks-indexed"
#define KTH_DB_TYPE_PRUNED "pruned"


#endif // KTH_NODE_EXE_EXECUTOR_INFO_HPP_
