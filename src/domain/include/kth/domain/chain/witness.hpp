// Copyright (c) 2016-2025 Knuth Project developers.
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

#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>

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

    bool operator==(witness const& x) const;
    bool operator!=(witness const& x) const;

    // Deserialization (from witness stack).
    //-------------------------------------------------------------------------
    // Prefixed data assumed valid here though caller may confirm with is_valid.

    /// Deserialization invalidates the iterator.
    template <typename R, KTH_IS_READER(R)>
    bool from_data(R& source, bool prefix) {
        reset();
        valid_ = true;

        auto const read_element = [](R& source) {
            // Tokens encoded as variable integer prefixed byte array (bip144).
            auto const size = source.read_size_little_endian();

            // The max_script_size and max_push_data_size constants limit
            // evaluation, but not all stacks evaluate, so use max_block_weight
            // to guard memory allocation here.
            if (size > max_block_weight) {
                source.invalidate();
                return data_chunk{};
            }

            return source.read_bytes(size);
        };

        // TODO(legacy): optimize store serialization to avoid loop, reading data directly.
        if (prefix) {
            // Witness prefix is an element count, not byte length (unlike script).
            // On wire each witness is prefixed with number of elements (bip144).
            for (auto count = source.read_size_little_endian(); count > 0; --count) {
                stack_.push_back(read_element(source));
            }
        } else {
            while ( ! source.is_exhausted()) {
                stack_.push_back(read_element(source));
            }
        }

        if ( ! source) {
            reset();
        }

        return source;
    }

    /// The witness deserialized ccording to count and size prefixing.
    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    data_chunk to_data(bool prefix) const;
    void to_data(data_sink& stream, bool prefix) const;

    template <typename W>
    void to_data(W& sink, bool prefix) const {
        // Witness prefix is an element count, not byte length (unlike script).
        if (prefix) {
            sink.write_size_little_endian(stack_.size());
        }

        auto const serialize = [&sink](data_chunk const& element) {
            // Tokens encoded as variable integer prefixed byte array (bip144).
            sink.write_size_little_endian(element.size());
            sink.write_bytes(element);
        };

        // TODO(legacy): optimize store serialization to avoid loop, writing data directly.
        std::for_each(stack_.begin(), stack_.end(), serialize);
    }

    //void to_data(writer& sink, bool prefix) const;

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

    code verify(transaction const& tx, uint32_t input_index, uint32_t forks, script const& program_script, uint64_t value) const;

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
