// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/byte_code_pattern.hpp>

#include <kth/domain/machine/operation.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

namespace kth::domain::chain {

byte_code_pattern make_byte_code_pattern(byte_span script) {
    byte_code_pattern result;
    size_t pushes = 0;
    auto const flush = [&] {
        if (pushes == 0) {
            return;
        }
        auto const count = infrastructure::machine::number::from_int(int64_t(pushes));
        KTH_CONTRACT(count.has_value());
        auto const encoded = machine::operation(count->data()).to_data();
        result.pattern.insert(result.pattern.end(), encoded.begin(), encoded.end());
        pushes = 0;
    };

    byte_reader reader(script);
    while (reader.remaining_size() != 0) {
        auto op = machine::operation::from_data(reader);
        if ( ! op) {
            result.error = true;
            break;
        }
        if (op->is_push()) {
            auto const code = op->code();
            if (code == machine::opcode::push_negative_1) {
                result.data.push_back(data_chunk{0x81});
            } else if (op->is_positive()) {
                result.data.push_back(data_chunk{machine::operation::opcode_to_positive(code)});
            } else {
                result.data.push_back(op->data());
            }
            ++pushes;
        } else {
            flush();
            result.pattern.push_back(uint8_t(op->code()));
        }
    }
    flush();
    if (result.error) {
        // Preserve the RPC's malformed-script suffix, including the byte
        // immediately before the decoder stopped.
        auto const at = reader.position();
        auto const from = at > 0 ? at - 1 : 0;
        result.data.emplace_back(script.begin() + from, script.end());
    }
    return result;
}

} // namespace kth::domain::chain
