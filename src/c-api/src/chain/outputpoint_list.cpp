// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/output_point.hpp>

#include <kth/capi/chain/outputpoint_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/list_creator.h>

KTH_LIST_DEFINE_CONVERTERS(chain, kth_outputpoint_list_t, kth::domain::chain::output_point, outputpoint_list)

// ---------------------------------------------------------------------------
extern "C" {

KTH_LIST_DEFINE(chain, kth_outputpoint_list_t, kth_outputpoint_t, outputpoint_list, kth::domain::chain::output_point, kth_chain_output_point_const_cpp)

} // extern "C"
