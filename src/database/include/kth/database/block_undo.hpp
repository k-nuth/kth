// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_BLOCK_UNDO_HPP
#define KTH_DATABASE_BLOCK_UNDO_HPP

#include <cstdint>
#include <expected>
#include <vector>

#include <kth/database/define.hpp>
#include <kth/database/databases/utxo_entry.hpp>
#include <kth/domain.hpp>

namespace kth::database {

/// Undo information for a single transaction.
/// Contains the UTXOs that were spent by this transaction's inputs.
struct KD_API tx_undo {
    std::vector<utxo_entry> prev_outputs;

    /// Serialized size in bytes.
    [[nodiscard]]
    size_t serialized_size() const;

    /// Serialize to data chunk.
    [[nodiscard]]
    data_chunk to_data() const;

    /// Serialize to writer.
    template <typename W, KTH_IS_WRITER(W)>
    void to_data(W& sink) const {
        sink.write_variable_little_endian(prev_outputs.size());
        for (auto const& entry : prev_outputs) {
            entry.to_data(sink);
        }
    }

    /// Deserialize from reader.
    [[nodiscard]]
    static std::expected<tx_undo, database::result_code> from_data(byte_reader& reader);
};

/// Undo information for a block.
/// Contains tx_undo for all transactions except the coinbase.
struct KD_API block_undo {
    std::vector<tx_undo> tx_undos;

    /// Serialized size in bytes.
    [[nodiscard]]
    size_t serialized_size() const;

    /// Serialize to data chunk.
    [[nodiscard]]
    data_chunk to_data() const;

    /// Serialize to writer.
    template <typename W, KTH_IS_WRITER(W)>
    void to_data(W& sink) const {
        sink.write_variable_little_endian(tx_undos.size());
        for (auto const& tx : tx_undos) {
            tx.to_data(sink);
        }
    }

    /// Deserialize from reader.
    [[nodiscard]]
    static std::expected<block_undo, database::result_code> from_data(byte_reader& reader);

    /// Create block undo from a block and the UTXOs it spends.
    /// @param block The block being connected.
    /// @param spent_utxos Map of output_point -> utxo_entry for all inputs.
    [[nodiscard]]
    static block_undo from_block(
        domain::chain::block const& block,
        std::function<utxo_entry(domain::chain::output_point const&)> const& get_utxo);
};

/// Result of disconnecting a block using undo data.
enum class disconnect_result {
    ok,           // Block successfully disconnected
    unclean,      // Disconnected but UTXO set was inconsistent
    failed        // Failed to disconnect
};

} // namespace kth::database

#endif // KTH_DATABASE_BLOCK_UNDO_HPP
