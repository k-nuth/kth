// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_BLOCK_UNDO_HPP
#define KTH_DATABASE_BLOCK_UNDO_HPP

#include <cstdint>
#include <expected>
#include <vector>

#include <utxoz/types.hpp>

#include <kth/database/define.hpp>
#include <kth/database/databases/result_code.hpp>
#include <kth/domain.hpp>

namespace kth::database {

/// One output spent by a block, recorded so the block can be disconnected.
///
/// `value` is the UTXO storage payload verbatim — in reference mode the 8-byte
/// {file_number, tx_offset} reference, in full mode the serialized entry — which
/// is exactly the shape apply_delta_raw's insert range consumes. Restoring a
/// spent output is therefore just re-inserting this record; no reconstruction of
/// the output is needed, and none is possible in reference mode (the storage holds
/// a reference into the block files, not the output bytes).
///
/// `height` is the output's ORIGINAL creation height, not the height of the block
/// that spent it. It must round-trip exactly: in reference mode the height also
/// drives the median-time-past window used when the UTXO is later resolved.
struct KD_API spent_output {
    utxoz::raw_outpoint key{};
    std::vector<uint8_t> value;
    uint32_t height{0};

    /// Serialized size in bytes.
    [[nodiscard]]
    size_t serialized_size() const;

    [[nodiscard]]
    static std::expected<spent_output, result_code> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const;
};

/// Undo information for a block: every output the block spent, keyed by outpoint.
///
/// The order of `spent` is unspecified — entries are identified by `key`, not by
/// position, and do NOT line up with the block's inputs (outputs a block both
/// creates and spends have no entry at all). Consumers must match by key.
///
/// Outputs *created* by the block are deliberately not recorded: they are
/// recomputable by re-reading the block from the flat files, so storing them
/// would roughly double the undo size for no gain (this mirrors BCHN).
struct KD_API block_undo {
    std::vector<spent_output> spent;

    /// Serialized size in bytes.
    [[nodiscard]]
    size_t serialized_size() const;

    [[nodiscard]]
    static std::expected<block_undo, result_code> from_data(byte_reader& reader);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer) const;

    [[nodiscard]]
    data_chunk to_data() const;
};

/// Result of disconnecting a block using undo data.
enum class disconnect_result {
    ok,           // Block successfully disconnected
    unclean,      // Disconnected but UTXO set was inconsistent
    failed        // Failed to disconnect
};

} // namespace kth::database

#endif // KTH_DATABASE_BLOCK_UNDO_HPP
