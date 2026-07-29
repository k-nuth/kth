// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_FRAMING_HPP
#define KTH_NODE_SV2_FRAMING_HPP

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

// Stratum V2 message framing. This is the plaintext frame that the Noise
// transport later wraps: a 6-byte header followed by the message payload. All
// integers are little-endian. See sv2-spec 04-Protocol-Overview.

namespace kth::node::sv2 {

// A U24 is the one SV2 fixed-width integer that byte_reader / byte_writer do not
// already cover; the rest (U8, U16, U32, U64) are their little-endian reads and
// writes. Used by the frame header (msg_length) and the B0_16M length prefix.
constexpr uint32_t u24_max = 0xffffffu;

[[nodiscard]]
expect<void> write_u24(byte_writer& sink, uint32_t value);

[[nodiscard]]
expect<uint32_t> read_u24(byte_reader& source);

// SV2 message frame header: extension_type (U16), msg_type (U8), msg_length
// (U24). The high bit of extension_type is the channel_msg flag; the low 15
// bits are the extension id.
struct message_header {
    static constexpr size_t size = 6;
    static constexpr uint16_t channel_msg_flag = 0x8000u;

    uint16_t extension_type = 0;
    uint8_t msg_type = 0;
    uint32_t msg_length = 0;   // U24: must be <= u24_max

    [[nodiscard]]
    bool channel_msg() const {
        return (extension_type & channel_msg_flag) != 0;
    }

    // The extension id with the channel_msg flag cleared.
    [[nodiscard]]
    uint16_t extension() const {
        return extension_type & static_cast<uint16_t>(~channel_msg_flag);
    }

    [[nodiscard]]
    size_t serialized_size() const {
        return size;
    }

    [[nodiscard]]
    expect<void> to_data(byte_writer& sink) const;

    [[nodiscard]]
    static expect<message_header> from_data(byte_reader& source);
};

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_FRAMING_HPP
