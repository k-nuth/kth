// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_WITNESS_HPP
#define KTH_DOMAIN_CHAIN_WITNESS_HPP

#include <cstddef>
#include <istream>
#include <string>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/multi_crypto_settings.hpp>

#include <kth/domain/machine/operation.hpp>

#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/thread.hpp>

#include <kth/domain/utils.hpp>
#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

#if ! defined(KTH_SEGWIT_ENABLED)

class witness {};

#else

struct KD_API witness {
    using operation = machine::operation;
    using iterator = data_stack::const_iterator;

    // Constructors.
    //-------------------------------------------------------------------------

    witness() = default;

    witness(data_stack&& stack);
    witness(data_stack const& stack);
    witness(data_chunk&& encoded, bool prefix);
    witness(data_chunk const& encoded, bool prefix);

    // Operators.
    //-------------------------------------------------------------------------

    // NOTE: `==` is not `= default` because the `valid_` member is internal
    // "has-been-parsed" state — equality is defined by the witness stack
    // alone. `!=` is defaulted (delegates to `!(==)`).
    bool operator==(witness const& x) const;
    bool operator!=(witness const& x) const = default;

    // Deserialization (from witness stack).
    //-------------------------------------------------------------------------
    // Prefixed data assumed valid here though caller may confirm with is_valid.

    /// Deserialization invalidates the iterator.
    bool from_data(byte_reader& reader, bool prefix);

    /// The witness deserialized ccording to count and size prefixing.
    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool prefix) const;

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, bool prefix) const;


    std::string to_string() const;

    // Iteration.
    //-------------------------------------------------------------------------

    void clear();
    bool empty() const;
    size_t size() const;
    data_chunk const& front() const;
    data_chunk const& back() const;
    iterator begin() const;
    iterator end() const;
    data_chunk const& operator[](size_t index) const;

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    size_t serialized_size(bool prefix) const;
    data_stack const& stack() const;

    // Utilities.
    //-------------------------------------------------------------------------

    static
    bool is_push_size(data_stack const& stack);

    static
    bool is_reserved_pattern(data_stack const& stack);

    bool extract_sigop_script(script& out_script, script const& program_script) const;
    bool extract_embedded_script(script& out_script, data_stack& out_stack, script const& program_script) const;

    // Validation.
    //-------------------------------------------------------------------------

    code verify(transaction const& tx, uint32_t input_index, script_flags_t flags, script const& program_script, uint64_t value) const;

// protected:
//     // So that input may call reset from its own.
//     friend class input;

    void reset();

private:
    static
    size_t serialized_size(data_stack const& stack);

    static
    operation::list to_pay_key_hash(data_chunk&& program);

    bool valid_{false};
    data_stack stack_;
};
#endif // KTH_CURRENCY_BCH

} // namespace kth::domain::chain

#endif
