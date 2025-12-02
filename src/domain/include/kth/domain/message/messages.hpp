// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_MESSAGES_HPP
#define KTH_DOMAIN_MESSAGE_MESSAGES_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <kth/domain/message/address.hpp>
#include <kth/domain/message/alert.hpp>
#include <kth/domain/message/alert_payload.hpp>
#include <kth/domain/message/block.hpp>
#include <kth/domain/message/block_transactions.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/double_spend_proof.hpp>
#include <kth/domain/message/fee_filter.hpp>
#include <kth/domain/message/filter_add.hpp>
#include <kth/domain/message/filter_clear.hpp>
#include <kth/domain/message/filter_load.hpp>
#include <kth/domain/message/get_address.hpp>
#include <kth/domain/message/get_block_transactions.hpp>
#include <kth/domain/message/get_blocks.hpp>
#include <kth/domain/message/get_data.hpp>
#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/headers.hpp>
#include <kth/domain/message/heading.hpp>
#include <kth/domain/message/inventory.hpp>
#include <kth/domain/message/inventory_vector.hpp>
#include <kth/domain/message/memory_pool.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/message/not_found.hpp>
#include <kth/domain/message/ping.hpp>
#include <kth/domain/message/pong.hpp>
#include <kth/domain/message/reject.hpp>
#include <kth/domain/message/send_compact.hpp>
#include <kth/domain/message/send_headers.hpp>
#include <kth/domain/message/transaction.hpp>
#include <kth/domain/message/verack.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/message/xverack.hpp>
#include <kth/domain/message/xversion.hpp>

#include <kth/infrastructure/message/network_address.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/limits.hpp>

// Minimum current kth protocol version:            31402
// Minimum current satoshi client protocol version: 31800

// kth-network
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// version      v2      70001           added relay field
// verack       v1
// getaddr      v1
// addr         v1
// ping         v1
// ping         v2      60001   BIP031  added nonce field
// pong         v1      60001   BIP031
// reject       v3      70002   BIP061
// ----------------------------------------------------------------------------
// alert        --                      no intent to support
// checkorder   --                      obsolete
// reply        --                      obsolete
// submitorder  --                      obsolete
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// kth-node
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// getblocks    v1
// inv          v1
// getdata      v1
// getdata      v3      70001   BIP037  allows filtered_block flag
// block        v1
// tx           v1
// notfound     v2      70001
// getheaders   v3      31800
// headers      v3      31800
// mempool      --      60002   BIP035
// mempool      v3      70002           allow multiple inv messages in reply
// sendheaders  v3      70012   BIP130
// feefilter    v3      70013   BIP133
// blocktxn     v3      70014   BIP152
// cmpctblock   v3      70014   BIP152
// getblocktxn  v3      70014   BIP152
// sendcmpct    v3      70014   BIP152
// merkleblock  v3      70001   BIP037  no bloom filters so unfiltered only
// ----------------------------------------------------------------------------
// filterload   --      70001   BIP037  no intent to support, see BIP111
// filteradd    --      70001   BIP037  no intent to support, see BIP111
// filterclear  --      70001   BIP037  no intent to support, see BIP111
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

namespace kth {

#define DECLARE_MESSAGE_POINTER_TYPES(type) \
    using type##_ptr = domain::message::type::ptr;  \
    using type##_const_ptr = domain::message::type::const_ptr

#define DECLARE_MESSAGE_POINTER_LIST_POINTER_TYPES(type)                 \
    using type##_ptr_list = domain::message::type::ptr_list;                     \
    using type##_const_ptr_list = domain::message::type::const_ptr_list;         \
    using type##_const_ptr_list_ptr = domain::message::type::const_ptr_list_ptr; \
    using type##_const_ptr_list_const_ptr = domain::message::type::const_ptr_list_const_ptr

// HACK: declare these in bc namespace to reduce length.
DECLARE_MESSAGE_POINTER_TYPES(address);
DECLARE_MESSAGE_POINTER_TYPES(block);
DECLARE_MESSAGE_POINTER_TYPES(block_transactions);
DECLARE_MESSAGE_POINTER_TYPES(compact_block);
DECLARE_MESSAGE_POINTER_TYPES(double_spend_proof);
DECLARE_MESSAGE_POINTER_TYPES(get_address);
DECLARE_MESSAGE_POINTER_TYPES(fee_filter);
DECLARE_MESSAGE_POINTER_TYPES(get_blocks);
DECLARE_MESSAGE_POINTER_TYPES(get_block_transactions);
DECLARE_MESSAGE_POINTER_TYPES(get_data);
DECLARE_MESSAGE_POINTER_TYPES(get_headers);
DECLARE_MESSAGE_POINTER_TYPES(header);
DECLARE_MESSAGE_POINTER_TYPES(headers);
DECLARE_MESSAGE_POINTER_TYPES(inventory);
DECLARE_MESSAGE_POINTER_TYPES(memory_pool);
DECLARE_MESSAGE_POINTER_TYPES(merkle_block);
DECLARE_MESSAGE_POINTER_TYPES(not_found);
DECLARE_MESSAGE_POINTER_TYPES(ping);
DECLARE_MESSAGE_POINTER_TYPES(pong);
DECLARE_MESSAGE_POINTER_TYPES(reject);
DECLARE_MESSAGE_POINTER_TYPES(send_compact);
DECLARE_MESSAGE_POINTER_TYPES(send_headers);
DECLARE_MESSAGE_POINTER_TYPES(transaction);
DECLARE_MESSAGE_POINTER_TYPES(verack);
DECLARE_MESSAGE_POINTER_TYPES(version);
DECLARE_MESSAGE_POINTER_LIST_POINTER_TYPES(block);
DECLARE_MESSAGE_POINTER_LIST_POINTER_TYPES(transaction);

#undef DECLARE_MESSAGE_POINTER_TYPES
#undef DECLARE_MESSAGE_POINTER_LIST_POINTER_TYPES

namespace domain::message {

/// Serialize a message object to the Bitcoin wire protocol encoding.
template <typename Message>
data_chunk serialize(uint32_t version, const Message& packet, uint32_t magic) {
    auto const heading_size = heading::satoshi_fixed_size();
    auto const payload_size = packet.serialized_size(version);
    auto const message_size = heading_size + payload_size;

    // Unfortunately data_sink doesn't support seek, so this is a little ugly.
    // The header requires payload size and checksum but prepends the payload.
    // Use a stream to prevent unnecessary copying of the payload.
    data_chunk data;

    // Reserve memory for the full message.
    data.reserve(message_size);

    // Size the vector for the heading so that payload insertion will follow.
    data.resize(heading_size);

    // Insert the payload after the heading and into the reservation.
    data_sink ostream(data);
    packet.to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == message_size);

    // Create the payload checksum without copying the buffer.
    byte_span span(data.data() + heading_size, data.data() + message_size);
    auto const check = bitcoin_checksum(span);
    auto const payload_size32 = *safe_unsigned<uint32_t>(payload_size);

    // Create and serialize the heading to a temporary variable (12 bytes).
    heading head(magic, Message::command, payload_size32, check);
    auto heading = head.to_data();

    // Move the heading into the allocated beginning of the message buffer.
    std::move(heading.begin(), heading.end(), data.begin());
    return data;
}

// KD_API size_t variable_uint_size(uint64_t value);

} // namespace domain::message
} // namespace kth

#endif
