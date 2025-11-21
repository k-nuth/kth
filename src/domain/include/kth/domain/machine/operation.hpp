// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_OPERATION_HPP
#define KTH_DOMAIN_MACHINE_OPERATION_HPP

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

#include <kth/domain/constants.hpp>
#include <kth/domain/machine/opcode.hpp>

#include <kth/domain/define.hpp>
//#include <kth/infrastructure/define.hpp>

#include <kth/infrastructure/machine/script_pattern.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>



#include <kth/domain/concepts.hpp>

namespace kth::domain::machine {

//TODO(fernando): static?
constexpr
// auto invalid_code = opcode::disabled_xor;
auto invalid_code = opcode::invalidopcode;

struct KD_API operation {
    using list = std::vector<operation>;
    using iterator = list::const_iterator;

    // Constructors.
    //-------------------------------------------------------------------------

    operation() = default;
    operation(data_chunk&& uncoded, bool minimal = true);
    operation(data_chunk const& uncoded, bool minimal = true);
    operation(opcode code);

    // Operators.
    //-------------------------------------------------------------------------

    bool operator==(operation const& x) const;
    bool operator!=(operation const& x) const;

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<operation> from_data(byte_reader& reader);

    bool from_string(std::string const& mnemonic);

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data() const;

    void to_data(data_sink& stream) const;

    template <typename W>
    void to_data(W& sink) const {
        auto const size = data_.size();

        sink.write_byte(static_cast<uint8_t>(code_));

        switch (code_) {
            case opcode::push_one_size:
                sink.write_byte(static_cast<uint8_t>(size));
                break;
            case opcode::push_two_size:
                sink.write_2_bytes_little_endian(static_cast<uint16_t>(size));
                break;
            case opcode::push_four_size:
                sink.write_4_bytes_little_endian(static_cast<uint32_t>(size));
                break;
            default:
                break;
        }

        sink.write_bytes(data_);
    }

    [[nodiscard]]
    std::string to_string(uint32_t active_forks) const;

    // Properties (size, accessors, cache).
    //-------------------------------------------------------------------------

    [[nodiscard]]
    size_t serialized_size() const;

    /// Get the op code [0..255], if is_valid is consistent with data.
    [[nodiscard]]
    opcode code() const;

    /// Get the data, empty if not a push code or if invalid.
    [[nodiscard]]
    data_chunk const& data() const;

    // Utilities.
    //-------------------------------------------------------------------------

    /// Compute nominal data opcode based on size alone.
    static
    opcode opcode_from_size(size_t size);

    /// Compute the minimal data opcode for a given chunk of data.
    /// Caller should clear data if converting to non-payload opcode.
    static
    opcode minimal_opcode_from_data(data_chunk const& data);

    /// Compute the nominal data opcode for a given chunk of data.
    /// Restricted to sized data, avoids conversion to numeric opcodes.
    static
    opcode nominal_opcode_from_data(data_chunk const& data);

    /// Convert the [1..16] value to the corresponding opcode (or undefined).
    static
    opcode opcode_from_positive(uint8_t value);

    /// Convert the opcode to the corresponding [1..16] value (or undefined).
    static
    uint8_t opcode_to_positive(opcode code);

    /// Categories of opcodes.
    static
    bool is_push(opcode code);

    static
    bool is_payload(opcode code);

    static
    bool is_counted(opcode code);

    static
    bool is_version(opcode code);

    static
    bool is_numeric(opcode code);

    static
    bool is_positive(opcode code);

    static
    bool is_reserved(opcode code);

    static
    bool is_disabled(opcode code, uint32_t active_forks);

    static
    bool is_conditional(opcode code);

    static
    bool is_relaxed_push(opcode code);

    /// Categories of operations.
    [[nodiscard]]
    bool is_push() const;

    [[nodiscard]]
    bool is_counted() const;

    [[nodiscard]]
    bool is_version() const;

    [[nodiscard]]
    bool is_positive() const;

    [[nodiscard]]
    bool is_disabled(uint32_t active_forks) const;

    [[nodiscard]]
    bool is_conditional() const;

    [[nodiscard]]
    bool is_relaxed_push() const;

    [[nodiscard]]
    bool is_oversized(size_t max_size) const;

    [[nodiscard]]
    bool is_minimal_push() const;

    [[nodiscard]]
    bool is_nominal_push() const;

protected:
    operation(opcode code, data_chunk&& data, bool valid);
    operation(opcode code, data_chunk const& data, bool valid);

    static
    expect<uint32_t> read_data_size(opcode code, byte_reader& reader);

    opcode opcode_from_data(data_chunk const& data, bool minimal);
    void reset();

private:
    opcode code_{invalid_code};
    data_chunk data_;
    bool valid_{false};
};

} // namespace kth::domain::machine

#include <kth/domain/impl/machine/operation.ipp>

#endif
