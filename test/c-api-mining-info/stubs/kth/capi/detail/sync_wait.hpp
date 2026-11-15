// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying file COPYING.
#pragma once
#include <kth/blockchain/interface/block_chain.hpp>
namespace kth::capi {
template<class Owner, class Operation>
auto sync_wait(Owner&, Operation&&) { return kth::blockchain::result{}; }
}
