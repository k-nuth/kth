// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/witness.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <iterator>
#include <numeric>
#include <string>
#include <utility>

#include <boost/algorithm/string.hpp>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/script_pattern.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/collection.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::domain::chain {

using namespace kth::domain::machine;

// Constructors.
//-----------------------------------------------------------------------------

witness::witness(data_stack const& stack) {
    stack_ = stack;
}

witness::witness(data_stack&& stack) {
    stack_ = std::move(stack);
}

witness::witness(data_chunk&& encoded, bool prefix) {
    entity_from_data(*this, static_cast<data_chunk const&>(encoded), prefix);
    // from_data(encoded, prefix);
}

witness::witness(data_chunk const& encoded, bool prefix) {
    entity_from_data(*this, encoded, prefix);
}


// witness::witness(witness&& x) noexcept
//     : stack_(std::move(x.stack_)), valid_(x.valid_) {
// }

// witness& witness::operator=(witness const& x) {
//     reset();
//     stack_ = x.stack_;
//     valid_ = x.valid_;
//     return *this;
// }

// witness& witness::operator=(witness&& x) noexcept {
//     reset();
//     stack_ = std::move(x.stack_);
//     valid_ = x.valid_;
//     return *this;
// }

// Operators.
//-----------------------------------------------------------------------------

bool witness::operator==(witness const& x) const {
    return stack_ == x.stack_;
}

bool witness::operator!=(witness const& x) const {
    return !(*this == x);
}

// private/static
size_t witness::serialized_size(data_stack const& stack) {
    auto const sum = [](size_t total, data_chunk const& element) {
        // Tokens encoded as variable integer prefixed byte array (bip144).
        auto const size = element.size();
        return total + infrastructure::message::variable_uint_size(size) + size;
    };

    return std::accumulate(stack.begin(), stack.end(), size_t(0), sum);
}

// protected
void witness::reset() {
    valid_ = false;
    stack_.clear();
    stack_.shrink_to_fit();
}

bool witness::is_valid() const {
    // Witness validity is consistent with stack validity (unlike script).
    return valid_;
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk witness::to_data(bool prefix) const {
    data_chunk data;
    auto const size = serialized_size(prefix);
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream, prefix);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void witness::to_data(data_sink& stream, bool prefix) const {
    ostream_writer sink_w(stream);
    to_data(sink_w, prefix);
}

//void witness::to_data(writer& sink, bool prefix) const
//{
//    // Witness prefix is an element count, not byte length (unlike script).
//    if (prefix)
//        sink.write_size_little_endian(stack_.size());
//
//    auto const serialize = [&sink](data_chunk const& element)
//    {
//        // Tokens encoded as variable integer prefixed byte array (bip144).
//        sink.write_size_little_endian(element.size());
//        sink.write_bytes(element);
//    };
//
//    // TODO(legacy): optimize store serialization to avoid loop, writing data directly.
//    std::for_each(stack_.begin(), stack_.end(), serialize);
//}

std::string witness::to_string() const {
    if ( ! valid_) {
        return "<invalid>";
    }

    std::string text;
    auto const serialize = [&text](data_chunk const& element) {
        text += "[" + encode_base16(element) + "] ";
    };

    std::for_each(stack_.begin(), stack_.end(), serialize);
    return boost::trim_copy(text);
}

// Iteration.
//-----------------------------------------------------------------------------
// These are syntactic sugar that allow the caller to iterate stack directly.

void witness::clear() {
    reset();
}

bool witness::empty() const {
    return stack_.empty();
}

size_t witness::size() const {
    return stack_.size();
}

data_chunk const& witness::front() const {
    KTH_ASSERT( ! stack_.empty());
    return stack_.front();
}

data_chunk const& witness::back() const {
    KTH_ASSERT( ! stack_.empty());
    return stack_.back();
}

data_chunk const& witness::operator[](size_t index) const {
    KTH_ASSERT(index < stack_.size());
    return stack_[index];
}

witness::iterator witness::begin() const {
    return stack_.begin();
}

witness::iterator witness::end() const {
    return stack_.end();
}

// Properties (size).
//-----------------------------------------------------------------------------

size_t witness::serialized_size(bool prefix) const {
    // Witness prefix is an element count, not a byte length (unlike script).
    return (prefix ? infrastructure::message::variable_uint_size(stack_.size()) : 0u) +
           serialized_size(stack_);
}

data_stack const& witness::stack() const {
    return stack_;
}

// Utilities.
//-----------------------------------------------------------------------------

// static
bool witness::is_push_size(data_stack const& stack) {
    auto const push_size = [](data_chunk const& element) {
        return element.size() <= max_push_data_size;
    };

    return std::all_of(stack.begin(), stack.end(), push_size);
}

// static
// The (only) coinbase witness must be (arbitrary) 32-byte value (bip141).
bool witness::is_reserved_pattern(data_stack const& stack) {
    return stack.size() == 1 &&
           stack[0].size() == hash_size;
}

// private
// This is an internal optimization over using script::to_pay_key_hash_pattern.
operation::list witness::to_pay_key_hash(data_chunk&& program) {
    KTH_ASSERT(program.size() == short_hash_size);

    return operation::list{
        {opcode::dup},
        {opcode::hash160},
        {std::move(program)},
        {opcode::equalverify},
        {opcode::checksig}};
}

// The return script is useful only for sigop counting.
// Returns true if is a witness program - even if potentially invalid.
bool witness::extract_sigop_script(script& out_script,
                                   script const& program_script) const {
    out_script.clear();

    switch (program_script.version()) {
        case script_version::zero: {
            switch (program_script.witness_program().size()) {
                case short_hash_size:
                    out_script.from_operations({{opcode::checksig}});
                    return true;

                case hash_size:
                    if ( ! stack_.empty()) {
                        entity_from_data(out_script, stack_.back(), false);
                    }

                    return true;

                default:
                    return true;
            }
        }

        case script_version::reserved:
            return true;

        case script_version::unversioned:
        default:
            return false;
    }
}

// Extract P2WPKH or P2WSH script as indicated by program script.
bool witness::extract_embedded_script(script& out_script, data_stack& out_stack, script const& program_script) const {
    switch (program_script.version()) {
        // The v0 program size must be either 20 or 32 bytes (bip141).
        case script_version::zero: {
            auto program = program_script.witness_program();
            auto const program_size = program.size();
            out_stack = stack_;

            // always: <signature> <pubkey>
            if (program_size == short_hash_size) {
                // Stack must be 2 elements, within push size limit (bip141).
                if (out_stack.size() != 2 || !is_push_size(out_stack)) {
                    return false;
                }

                // The hash160 of public key must match the program (bip141).
                out_script.from_operations(to_pay_key_hash(std::move(program)));
                return true;
            }

            // example: 0 <signature1> <1 <pubkey1> <pubkey2> 2 CHECKMULTISIG>
            if (program_size == hash_size) {
                // The witness must consist of at least 1 item (bip141).
                if (out_stack.empty()) {
                    return false;
                }

                // The script is popped off the initial witness stack (bip141).
                entity_from_data(out_script, pop(out_stack), false);

                // Stack elements must be within push size limit (bip141).
                if ( ! is_push_size(out_stack)) {
                    return false;
                }

                // SHA256 of the witness script must match program (bip141).
                return std::equal(program.begin(), program.end(),
                                  sha256_hash(out_script.to_data(false)).begin());
            }

            return false;
        }

        // These versions are reserved for future extensions (bip141).
        case script_version::reserved:
            return true;

        case script_version::unversioned:
        default:
            return false;
    }
}

// Validation.
//-----------------------------------------------------------------------------

// static
// The program script is either a prevout script or an emedded script.
// It validates this witness, from which the witness script is derived.
code witness::verify(transaction const& tx, uint32_t input_index, uint32_t forks, script const& program_script, uint64_t value) const {
    auto const version = program_script.version();

    switch (version) {
        case script_version::zero: {
            code ec;
            script script;
            data_stack stack;

            if ( ! extract_embedded_script(script, stack, program_script)) {
                return error::invalid_witness;
            }

            program witness(script, tx, input_index, forks, std::move(stack),
                            value, version);

            if ((ec = witness.evaluate())) {
                return ec;
            }

            // A v0 script must succeed with a clean true stack (bip141).
            if ( ! witness.stack_result(true)) {
                return error::stack_false;
            }
        }

        // These versions are reserved for future extensions (bip141).
        case script_version::reserved:
            return error::success;

        case script_version::unversioned:
        default:
            return error::operation_failed;
    }
}

} // namespace kth::domain::chain
