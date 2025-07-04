// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/machine/script_execution_context.hpp>

namespace kth::domain::machine {

script_execution_context::script_execution_context(uint32_t input_index, chain::transaction const& transaction)
    : input_index_(input_index), transaction_(transaction) {
}

uint32_t script_execution_context::input_index() const {
    return input_index_;
}

chain::transaction const& script_execution_context::transaction() const {
    return transaction_;
}

uint32_t script_execution_context::input_count() const {
    return static_cast<uint32_t>(transaction_.inputs().size());
}

uint32_t script_execution_context::output_count() const {
    return static_cast<uint32_t>(transaction_.outputs().size());
}

uint32_t script_execution_context::tx_version() const {
    return transaction_.version();
}

uint32_t script_execution_context::tx_locktime() const {
    return transaction_.locktime();
}

} // namespace kth::domain::machine