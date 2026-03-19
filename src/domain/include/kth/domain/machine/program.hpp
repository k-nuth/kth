// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_PROGRAM_HPP
#define KTH_DOMAIN_MACHINE_PROGRAM_HPP

#include <cstdint>
#include <expected>
#include <optional>
#include <tuple>
#include <utility>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/metrics.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/script_execution_context.hpp>
#include <kth/infrastructure/machine/big_number.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::machine {

using operation = ::kth::domain::machine::operation;        //TODO(fernando): why this?

#if ! defined(KTH_CURRENCY_BCH)
using script_version = ::kth::infrastructure::machine::script_version;
#endif // ! KTH_CURRENCY_BCH

using number = ::kth::infrastructure::machine::number;
using big_number = ::kth::infrastructure::machine::big_number;

struct KD_API program {
    using value_type = data_stack::value_type;
    using op_iterator = operation::iterator;

    using stack_iterator = data_stack::const_iterator;
    using stack_mutable_iterator = data_stack::iterator;

    /// Create an instance that does not expect to verify signatures.
    /// This is useful for script utilities but not with input validation.
    /// This can only run individual operations via run(op, program).
    program();

    /// Create an instance that does not expect to verify signatures.
    /// This is useful for script utilities but not with input validation.
    /// This can run ops via run(op, program) or the script via run(program).
    program(chain::script const& script);

    /// Create an instance with empty stacks (input run).
    program(chain::script const& script, chain::transaction const& transaction, uint32_t input_index, script_flags_t flags, uint64_t value = max_uint64);

    /// Create an instance with initialized stack (witness run, v0 by default).
    program(
        chain::script const& script
        , chain::transaction const& transaction
        , uint32_t input_index
        , script_flags_t flags
        , data_stack&& stack
        , uint64_t value
#if ! defined(KTH_CURRENCY_BCH)
        , script_version version = script_version::zero
#endif // ! KTH_CURRENCY_BCH
    );

    /// Create using copied tx, input, flags, value, stack (prevout run).
    program(chain::script const& script, program const& x);

    /// Create using copied tx, input, flags, value and moved stack (p2sh run).
    program(chain::script const& script, program&& x, bool move);

    [[nodiscard]]
    metrics& get_metrics();

    [[nodiscard]]
    metrics const& get_metrics() const;

    /// Constant registers.
    [[nodiscard]]
    bool is_valid() const;

    [[nodiscard]]
    script_flags_t flags() const;

    [[nodiscard]]
    size_t max_script_element_size() const;

    [[nodiscard]]
    size_t max_integer_size_legacy() const;

    [[nodiscard]]
    size_t max_integer_size() const;

    [[nodiscard]]
    bool is_chip_vm_limits_enabled() const;

    [[nodiscard]]
    bool is_bigint_enabled() const;

    [[nodiscard]]
    uint32_t input_index() const;

    [[nodiscard]]
    uint64_t value() const;

#if ! defined(KTH_CURRENCY_BCH)
    [[nodiscard]]
    script_version version() const;
#endif // ! KTH_CURRENCY_BCH

    [[nodiscard]]
    chain::transaction const& transaction() const;

    /// Get the script execution context (if available)
    [[nodiscard]]
    std::optional<script_execution_context> const& context() const;

    /// Program registers.
    [[nodiscard]]
    op_iterator begin() const;

    [[nodiscard]]
    op_iterator jump() const;

    [[nodiscard]]
    op_iterator end() const;

    /// Active script override for OP_INVOKE (OP_ACTIVEBYTECODE support).
    [[nodiscard]]
    chain::script const& get_script() const;
    void set_active_script(chain::script const* s);
    void reset_active_script();

    [[nodiscard]]
    size_t operation_count() const;

    /// Instructions.
    [[nodiscard]]
    code evaluate();
    [[nodiscard]]
    code evaluate(operation const& op);
    
    [[nodiscard]]
    bool increment_operation_count(operation const& op);
    [[nodiscard]]
    bool increment_operation_count(int32_t public_keys);
    [[nodiscard]]
    bool set_jump_register(operation const& op, int32_t offset);

    // Primary stack.
    //-------------------------------------------------------------------------

    /// Primary push.
    void push(bool value);
    void push_move(value_type&& item);
    void push_copy(value_type const& item);

    // Like pop() but does not return the value, for when the value is not needed.
    void drop();

    /// Primary pop.
    [[nodiscard]]
    data_chunk pop();
    [[nodiscard]] 
    std::expected<int32_t, error::error_code_t> pop_int32();
    [[nodiscard]] 
    std::expected<int64_t, error::error_code_t> pop_int64();
    [[nodiscard]]
    std::expected<number, error::error_code_t> pop_number(size_t maximum_size);
    [[nodiscard]]
    std::expected<std::pair<number, number>, error::error_code_t> pop_binary();
    [[nodiscard]]
    std::expected<std::tuple<number, number, number>, error::error_code_t> pop_ternary();

    [[nodiscard]]
    std::expected<big_number, error::error_code_t> pop_big_number(size_t maximum_size);
    [[nodiscard]]
    std::expected<std::pair<big_number, big_number>, error::error_code_t> pop_big_binary();
    [[nodiscard]]
    std::expected<std::tuple<big_number, big_number, big_number>, error::error_code_t> pop_big_ternary();    
    [[nodiscard]] 
    std::expected<uint32_t, error::error_code_t> pop_index();
    std::expected<stack_iterator, error::error_code_t> pop_position();
    [[nodiscard]] 
    std::expected<data_stack, error::error_code_t> pop(size_t count);

    /// Primary push/pop optimizations (active).
    void duplicate(size_t index);

    void swap(size_t index_left, size_t index_right);

    void erase(stack_iterator const& position);
    void erase(stack_iterator const& first, stack_iterator const& last);

    /// Primary push/pop optimizations (passive).
    [[nodiscard]]
    bool empty() const;

    [[nodiscard]]
    bool stack_true(bool clean) const;

    [[nodiscard]]
    bool stack_result(bool clean) const;

    [[nodiscard]]
    bool is_stack_overflow() const;
    bool is_stack_overflow(size_t extra) const;

    [[nodiscard]]
    bool if_(operation const& op) const;

    [[nodiscard]]
    value_type const& item(size_t index) const;

    [[nodiscard]]
    value_type& item(size_t index);

    [[nodiscard]]    
    data_chunk const& top() const;
    [[nodiscard]]
    data_chunk& top();
    [[nodiscard]]
    std::expected<number, error::error_code_t> top_number(size_t maximum_size) const;
    [[nodiscard]]
    std::expected<big_number, error::error_code_t> top_big_number(size_t maximum_size) const;

    [[nodiscard]]
    stack_iterator position(size_t index) const;
    [[nodiscard]]
    stack_mutable_iterator position(size_t index);

    [[nodiscard]]
    size_t index(stack_iterator const& position) const;

    [[nodiscard]]
    operation::list subscript() const;

    [[nodiscard]]
    size_t size() const;

    [[nodiscard]]
    size_t conditional_stack_size() const;

    // Alternate stack.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    bool empty_alternate() const;

    void push_alternate(value_type&& value);
    value_type pop_alternate();

    // Conditional stack.
    //-------------------------------------------------------------------------

    void open(bool value);
    void negate();
    void close();

    [[nodiscard]]
    bool closed() const;

    [[nodiscard]]
    bool succeeded() const;


private:
    // A space-efficient dynamic bitset (specialized).
    using bool_stack = std::vector<bool>;

    void reserve_stacks();

    [[nodiscard]]
    bool stack_to_bool(bool clean) const;

    chain::script const& script_;
    chain::script const* active_script_{nullptr};  // override for OP_ACTIVEBYTECODE during INVOKE
    chain::transaction const& transaction_;
    uint32_t const input_index_{0};
    script_flags_t const flags_{0};
    uint64_t const value_{0};

#if ! defined(KTH_CURRENCY_BCH)
    script_version version_{script_version::unversioned};
#endif // ! KTH_CURRENCY_BCH

    size_t negative_count_{0};
    size_t operation_count_{0};
    op_iterator jump_;
    data_stack primary_;
    data_stack alternate_;
    bool_stack condition_;

    metrics metrics_;
    
    /// Optional script execution context for Native Introspection opcodes
    std::optional<script_execution_context> context_;
};

} // namespace kth::domain::machine

#include <kth/domain/impl/machine/program.ipp>

#endif
