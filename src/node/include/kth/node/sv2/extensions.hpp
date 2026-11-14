// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SV2_EXTENSIONS_HPP
#define KTH_NODE_SV2_EXTENSIONS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Stratum V2 protocol extensions (sv2-spec 3.4 and extensions/). Extensions
// Negotiation (0x0001) defines the three messages below, framed with its own
// extension type. Worker-Specific Hashrate Tracking (0x0002) defines no
// message: it appends one TLV field to SubmitSharesExtended, whose frame keeps
// extension type 0 (sv2-spec 3.4.1).

namespace kth::node::sv2 {

inline constexpr uint16_t extensions_negotiation_type = 0x0001;
inline constexpr uint16_t worker_hashrate_tracking_extension = 0x0002;

// The extension's one field: the worker's user_identity, UTF-8 and at most
// 32 bytes, never padded.
inline constexpr uint8_t user_identity_field = 0x01;
inline constexpr size_t max_user_identity_size = 32;

// sv2-spec 3.4.3: a field longer than its extension allows rejects the message.
inline constexpr std::string_view error_user_identity_too_long = "user-identity-too-long";

struct request_extensions {
    static constexpr uint8_t message_type = 0x00;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = extensions_negotiation_type;

    uint16_t request_id = 0;
    std::vector<uint16_t> requested_extensions;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_extensions> from_data(byte_reader& source);
};

struct request_extensions_success {
    static constexpr uint8_t message_type = 0x01;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = extensions_negotiation_type;

    uint16_t request_id = 0;
    std::vector<uint16_t> supported_extensions;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_extensions_success> from_data(byte_reader& source);
};

struct request_extensions_error {
    static constexpr uint8_t message_type = 0x02;
    static constexpr bool channel_message = false;
    static constexpr uint16_t extension_id = extensions_negotiation_type;

    uint16_t request_id = 0;
    std::vector<uint16_t> unsupported_extensions;
    std::vector<uint16_t> required_extensions;

    [[nodiscard]] size_t serialized_size() const;
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<request_extensions_error> from_data(byte_reader& source);
};

// One TLV field after a message's base fields (sv2-spec 3.4.3): extension
// type U16, field type U8, length U16, then that many value bytes. The U16s
// are little-endian like every SV2 integer (sv2-spec 3.1), as the reference
// implementation encodes them; the byte examples in 3.4.3 and in extension
// 0x0002 print them big-endian.
struct tlv_field {
    uint16_t extension_type = 0;
    uint8_t field_type = 0;
    data_chunk value;

    [[nodiscard]] size_t serialized_size() const { return 2 + 1 + 2 + value.size(); }
    [[nodiscard]] expect<void> to_data(byte_writer& sink) const;
    [[nodiscard]] static expect<tlv_field> from_data(byte_reader& source);
};

// Every field left in `source`, in order. Fails when one is cut short.
[[nodiscard]]
expect<std::vector<tlv_field>> read_tlv_fields(byte_reader& source);

// The user_identity field for `identity`. A longer or non-UTF-8 identity is
// cut to its longest valid UTF-8 prefix of at most max_user_identity_size
// bytes, ending between code points.
[[nodiscard]]
tlv_field user_identity_tlv(std::string_view identity);

} // namespace kth::node::sv2

#endif // KTH_NODE_SV2_EXTENSIONS_HPP
