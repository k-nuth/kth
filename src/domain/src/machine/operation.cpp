// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/machine/operation.hpp>

#include <charconv>
#include <string>

#include <boost/algorithm/string.hpp>

// #include <kth/domain/constants.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::machine {

inline bool is_push_token(std::string const& token) {
    return token.size() > 1 && token.front() == '[' && token.back() == ']';
}

inline bool is_text_token(std::string const& token) {
    return token.size() > 1 && token.front() == '\'' && token.back() == '\'';
}

inline bool is_valid_data_size(opcode code, size_t size) {
    constexpr auto op_75 = static_cast<uint8_t>(opcode::push_size_75);
    auto const value = static_cast<uint8_t>(code);
    return value > op_75 || value == size;
}

inline std::string trim_token(std::string const& token) {
    KTH_ASSERT(token.size() > 1);
    return std::string(token.begin() + 1, token.end() - 1);
}

inline
string_list split_push_token(std::string const& token) {
    return split(trim_token(token), ".", false);
}

static
bool opcode_from_data_prefix(opcode& out_code,
                                    std::string const& prefix,
                                    data_chunk const& data) {
    constexpr auto op_75 = static_cast<uint8_t>(opcode::push_size_75);
    auto const size = data.size();
    out_code = operation::opcode_from_size(size);

    if (prefix == "0") {
        return size <= op_75;
    }
    if (prefix == "1") {
        out_code = opcode::push_one_size;
        return size <= max_uint8;
    }
    if (prefix == "2") {
        out_code = opcode::push_two_size;
        return size <= max_uint16;
    }
    if (prefix == "4") {
        out_code = opcode::push_four_size;
        return size <= max_uint32;
    }

    return false;
}

static
bool data_from_number_token(data_chunk& out_data, std::string const& token) {
    int64_t value;
    auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc()) {
        return false;
    }
    auto num_exp = number::from_int(value);
    if ( ! num_exp) {
        return false;
    }
    out_data = num_exp->data();
    return true;
}

// The removal of spaces in v3 data is a compatability break with our v2.
bool operation::from_string(std::string const& mnemonic) {
    reset();

    if (is_push_token(mnemonic)) {
        // Data encoding uses single token (with optional non-minimality).
        auto const parts = split_push_token(mnemonic);

        if (parts.size() == 1) {
            // Extract operation using nominal data size encoding.
            if (auto decoded = decode_base16(parts[0])) {
                data_ = std::move(*decoded);
                code_ = nominal_opcode_from_data(data_);
                valid_ = true;
            }
        } else if (parts.size() == 2) {
            // Extract operation using explicit data size encoding.
            if (auto decoded = decode_base16(parts[1])) {
                data_ = std::move(*decoded);
                valid_ = opcode_from_data_prefix(code_, parts[0], data_);
            }
        }
    } else if (is_text_token(mnemonic)) {
        auto const text = trim_token(mnemonic);
        data_ = data_chunk{text.begin(), text.end()};
        code_ = nominal_opcode_from_data(data_);
        valid_ = true;
    } else if (opcode_from_string(code_, mnemonic)) {
        // push_one_size, push_two_size and push_four_size succeed with empty.
        // push_size_1 through push_size_75 always fail because they are empty.
        valid_ = is_valid_data_size(code_, data_.size());
    } else if (data_from_number_token(data_, mnemonic)) {
        // [-1, 0, 1..16] integers captured by opcode_from_string, others here.
        // Otherwise minimal_opcode_from_data could convert integers here.
        code_ = nominal_opcode_from_data(data_);
        valid_ = true;
    } else {
        reset();
        valid_ = false;
        return false;
    }

    if ( ! valid_) {
        reset();
    }

    return valid_;
}

bool operation::is_valid() const {
    return valid_;
}

// protected
void operation::reset() {
    code_ = invalid_code;
    data_.clear();
    valid_ = false;
}

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<operation> operation::from_data(byte_reader& reader) {
    auto code_exp = reader.read_byte();
    if ( ! code_exp) {
        return std::unexpected(code_exp.error());
    }
    auto code = opcode(*code_exp);
    auto const size = read_data_size(code, reader);
    if ( ! size) {
        return std::unexpected(size.error());
    }
    auto data = reader.read_bytes(*size);
    if ( ! data) {
        return std::unexpected(data.error());
    }
    
    // For numeric opcodes, create operation directly with the opcode (no data)
    if (*size == 0 && is_numeric(code)) {
        return operation(code);
    }
    
    // For other opcodes, create operation with data using protected constructor
    data_chunk data_chunk_result(data->data(), data->data() + data->size());
    return operation(code, std::move(data_chunk_result), true);
}

// Serialization.
//-----------------------------------------------------------------------------

data_chunk operation::to_data() const {
    data_chunk data;
    auto const size = serialized_size();
    data.reserve(size);
    data_sink ostream(data);
    to_data(ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void operation::to_data(data_sink& stream) const {
    ostream_writer sink_w(stream);
    to_data(sink_w);
}

static
std::string opcode_to_prefix(opcode code, data_chunk const& data) {
    // If opcode is minimal for a size-based encoding, do not set a prefix.
    if (code == operation::opcode_from_size(data.size())) {
        return "";
    }

    switch (code) {
        case opcode::push_one_size:
            return "1.";
        case opcode::push_two_size:
            return "2.";
        case opcode::push_four_size:
            return "4.";
        default:
            return "0.";
    }
}

// The removal of spaces in v3 data is a compatability break with our v2.
std::string operation::to_string(uint32_t active_forks) const {
    if ( ! valid_) {
        return "<invalid>";
    }

    if (data_.empty()) {
        return opcode_to_string(code_, active_forks);
    }

    // Data encoding uses single token with explicit size prefix as required.
    return "[" + opcode_to_prefix(code_, data_) + encode_base16(data_) + "]";
}

} // namespace kth::domain::machine
