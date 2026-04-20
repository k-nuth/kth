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

// Forward decl — full definition lives in `interpreter.hpp`, which
// itself includes this header; importing it back here would cycle.
struct op_result;

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

    // LIFETIME CONTRACT for every `program` constructor below:
    //   `program` stores *pointers*, not copies, to its `script` and
    //   `transaction` arguments (see `script_` and `transaction_`
    //   below). Both referents MUST outlive every `program` built
    //   from them — including any `debug_snapshot` that holds a
    //   program by value. Passing temporaries is undefined behaviour;
    //   prefer named locals or static storage. Rvalue overloads are
    //   intentionally NOT `= delete`-d because legitimate callers
    //   pass `std::move(prev_program)` (lvalue moved into the new
    //   program), and C++ cannot distinguish that from a pure
    //   temporary at the signature level.

    /// Create an instance that does not expect to verify signatures.
    /// This is useful for script utilities but not with input validation.
    /// This can run ops via run(op, program) or the script via run(program).
    /// \pre `script` outlives the returned program.
    program(chain::script const& script);

    /// Create an instance with empty stacks (input run).
    /// \pre `script` and `transaction` outlive the returned program.
    program(chain::script const& script, chain::transaction const& transaction, uint32_t input_index, script_flags_t flags, uint64_t value = max_uint64);

    /// Create an instance with initialized stack (witness run, v0 by default).
    /// \pre `script` and `transaction` outlive the returned program.
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
    /// \pre `script` outlives the returned program; `x`'s script and
    /// transaction referents must also outlive this new instance (their
    /// addresses are copied over).
    program(chain::script const& script, program const& x);

    /// Create using copied tx, input, flags, value and moved stack (p2sh run).
    /// \pre `script` outlives the returned program. `x` must be an
    /// lvalue whose script/transaction referents outlive this new
    /// instance — `program(script, std::move(named_lvalue), true)` is
    /// the intended shape, NOT `program(script, make_program(), true)`.
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
    /// Returns the rich `op_result` so callers can see which opcode
    /// failed. The conversion to `kth::code` for legacy call sites is
    /// intentionally `explicit` (see `op_result::operator code()`);
    /// bridge at the call site with direct-init — `code ec{prog.evaluate()};`
    /// — or read the `.error` member directly to preserve polarity.
    [[nodiscard]]
    op_result evaluate();
    [[nodiscard]]
    op_result evaluate(operation const& op);
    
    [[nodiscard]]
    bool increment_operation_count(operation const& op);
    [[nodiscard]]
    bool increment_operation_count(int32_t public_keys);
    /// Anchor the active-bytecode-hash marker at the op immediately
    /// after `pc` (the zero-based index of the `OP_CODESEPARATOR` in
    /// the active script — the only opcode in Bitcoin script that
    /// touches this marker). Taking `pc` as an explicit argument
    /// avoids hidden program-level state that callers would otherwise
    /// have to publish separately before each dispatch. Returns
    /// `false` if the active script is empty or `pc` is out-of-range
    /// (guards the C-API surface against UB from a mis-sized index).
    [[nodiscard]]
    bool mark_code_separator(size_t pc);

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

    // Pointers (not references) so that `program` itself is
    // move-assignable — debug tooling holds snapshots by value and
    // the batch-step loops in interpreter.cpp rebind them. The
    // pointed-to script and transaction are still logically immutable;
    // the indirection is purely a storage choice.
    chain::script const* script_{nullptr};
    chain::transaction const* transaction_{nullptr};
    uint32_t input_index_{0};
    script_flags_t flags_{0};
    uint64_t value_{0};

    // Per-frame state pushed on OP_INVOKE, popped on return. Each
    // entry stores the overriding `script` and the frame's own
    // OP_CODESEPARATOR marker (`jump`). Empty stack means "outermost
    // frame — use `script_` / `jump_`". A previous revision stored
    // only a single-slot `active_script_` pointer; returning from a
    // nested OP_INVOKE then always fell back to the outermost script
    // and lost the caller function's active bytecode — a consensus
    // split once BCH 2026-May (subroutines) activates. BCHN's
    // `EvalStack` uses the same per-frame model.
    struct active_frame {
        chain::script const* script;
        op_iterator jump;
    };
    std::vector<active_frame> active_frames_;

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
