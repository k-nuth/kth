// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/block_undo.hpp>

#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::database {

// =============================================================================
// tx_undo
// =============================================================================

size_t tx_undo::serialized_size() const {
    size_t size = infrastructure::message::variable_uint_size(prev_outputs.size());
    for (auto const& entry : prev_outputs) {
        size += entry.serialized_size();
    }
    return size;
}

data_chunk tx_undo::to_data() const {
    data_chunk data;
    data.reserve(serialized_size());
    data_sink ostream(data);
    ostream_writer sink(ostream);
    to_data(sink);
    ostream.flush();
    return data;
}

std::expected<tx_undo, database::result_code> tx_undo::from_data(byte_reader& reader) {
    tx_undo result;

    auto const count = reader.read_variable_little_endian();
    if ( ! reader) {
        return std::unexpected(result_code::other);
    }

    result.prev_outputs.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        auto entry = utxo_entry::from_data(reader);
        if ( ! entry) {
            return std::unexpected(result_code::other);
        }
        result.prev_outputs.push_back(std::move(*entry));
    }

    return result;
}

// =============================================================================
// block_undo
// =============================================================================

size_t block_undo::serialized_size() const {
    size_t size = infrastructure::message::variable_uint_size(tx_undos.size());
    for (auto const& tx : tx_undos) {
        size += tx.serialized_size();
    }
    return size;
}

data_chunk block_undo::to_data() const {
    data_chunk data;
    data.reserve(serialized_size());
    data_sink ostream(data);
    ostream_writer sink(ostream);
    to_data(sink);
    ostream.flush();
    return data;
}

std::expected<block_undo, database::result_code> block_undo::from_data(byte_reader& reader) {
    block_undo result;

    auto const count = reader.read_variable_little_endian();
    if ( ! reader) {
        return std::unexpected(result_code::other);
    }

    result.tx_undos.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        auto tx = tx_undo::from_data(reader);
        if ( ! tx) {
            return std::unexpected(result_code::other);
        }
        result.tx_undos.push_back(std::move(*tx));
    }

    return result;
}

block_undo block_undo::from_block(
    domain::chain::block const& block,
    std::function<utxo_entry(domain::chain::output_point const&)> const& get_utxo)
{
    block_undo result;
    auto const& txs = block.transactions();

    // Skip coinbase (index 0), process all other transactions
    if (txs.size() > 1) {
        result.tx_undos.reserve(txs.size() - 1);

        for (size_t tx_idx = 1; tx_idx < txs.size(); ++tx_idx) {
            auto const& tx = txs[tx_idx];
            tx_undo tx_undo_data;
            tx_undo_data.prev_outputs.reserve(tx.inputs().size());

            for (auto const& input : tx.inputs()) {
                auto const& prevout = input.previous_output();
                tx_undo_data.prev_outputs.push_back(get_utxo(prevout));
            }

            result.tx_undos.push_back(std::move(tx_undo_data));
        }
    }

    return result;
}

} // namespace kth::database
