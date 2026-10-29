// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_SRC_NODE_PRIVATE_HPP_
#define KTH_CAPI_SRC_NODE_PRIVATE_HPP_

// Private to the C-API sources, not installed. `node.cpp` (hand-written)
// defines these; `node_generated.cpp` (generated) uses them.

#include <kth/capi/node.h>

#include <kth/node/executor/executor.hpp>

kth::node::executor const& kth_node_const_cpp(kth_node_t o);

kth::node::executor& kth_node_cpp(kth_node_t o);

#endif // KTH_CAPI_SRC_NODE_PRIVATE_HPP_
