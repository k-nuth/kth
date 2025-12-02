// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/machine/program.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/interpreter.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::machine {

using namespace kth::domain::chain;

// Fixed tuning parameters, max_stack_size ensures no reallocation.
static constexpr
size_t stack_capactity = max_stack_size;

static constexpr
size_t condition_capactity = max_counted_ops;

static
chain::transaction const default_tx_;

static
chain::script const default_script_;

void program::reserve_stacks() {
    primary_.reserve(stack_capactity);
    alternate_.reserve(stack_capactity);
    condition_.reserve(condition_capactity);
}

// Constructors.
//-----------------------------------------------------------------------------

program::program()
    : script_(default_script_),
      transaction_(default_tx_),
      jump_(script_.begin())
{
    reserve_stacks();
}

program::program(script const& script)
    : script_(script),
      transaction_(default_tx_),
      jump_(script_.begin()) {
    reserve_stacks();
}

program::program(script const& script, chain::transaction const& transaction, uint32_t input_index, uint32_t forks)
    : script_(script),
      transaction_(transaction),
      input_index_(input_index),
      forks_(forks),
      value_(max_uint64),
      jump_(script_.begin()) {
    reserve_stacks();
}

// Condition, alternate, jump and operation_count are not copied.
program::program(
  script const& script
  , chain::transaction const& transaction
  , uint32_t input_index
  , uint32_t forks
  , data_stack&& stack
  , uint64_t value
#if ! defined(KTH_CURRENCY_BCH)
  , script_version version
#endif // ! KTH_CURRENCY_BCH
)
    : script_(script),
      transaction_(transaction),
      input_index_(input_index),
      forks_(forks),
      value_(value),
#if ! defined(KTH_CURRENCY_BCH)
      version_(version),
#endif // ! KTH_CURRENCY_BCH
      jump_(script_.begin()),
      primary_(std::move(stack)) {
    reserve_stacks();
}

// Condition, alternate, jump and operation_count are not copied.
program::program(script const& script, program const& x)
    : script_(script),
      transaction_(x.transaction_),
      input_index_(x.input_index_),
      forks_(x.forks_),
      value_(x.value_),
      jump_(script_.begin()),
      primary_(x.primary_) {
    reserve_stacks();
}

// Condition, alternate, jump and operation_count are not moved.
program::program(script const& script, program&& x, bool /*unused*/)
    : script_(script),
      transaction_(x.transaction_),
      input_index_(x.input_index_),
      forks_(x.forks_),
      value_(x.value_),
      jump_(script_.begin()),
      primary_(std::move(x.primary_)) {
    reserve_stacks();
}

// Instructions.
//-----------------------------------------------------------------------------

code program::evaluate() {
    return interpreter::run(*this);
}

code program::evaluate(operation const& op) {
    return interpreter::run(op, *this);
}

} // namespace kth::domain::machine
