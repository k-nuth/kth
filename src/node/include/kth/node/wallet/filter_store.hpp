// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_FILTER_STORE_HPP
#define KTH_NODE_WALLET_FILTER_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/define.hpp>

namespace kth::node::wallet {

// What a node that holds only headers and filters keeps of one filter type,
// to answer its wallet from peers: for every block from a first height on, its
// hash, its filter header and its filter, in two flat files that only grow
// and are cut back at the fork of a reorganization. A full node keeps none of
// this: it answers from its own blocks.
//
// filters-<type>.dat holds the filters back to back. filters-<type>.idx starts
// with a 40-byte preamble (4 bytes of format, the first height, the filter
// header of the block before it) and has a 68-byte record per height (block
// hash, filter header, filter length). Nothing in them is per script, address
// or transaction: a scan reads them in order.

/// The filter header that follows `previous`: the double SHA-256 of the
/// filter's double SHA-256 followed by `previous`.
[[nodiscard]]
KND_API
hash_digest next_filter_header(byte_span filter, hash_digest const& previous);

/// The filter header from the filter hash a `cfheaders` lists.
[[nodiscard]]
KND_API
hash_digest next_filter_header_from_hash(hash_digest const& filter_hash, hash_digest const& previous);

/// One block's filter, appended in height order.
struct filter_entry {
    hash_digest block;
    hash_digest header;
    data_chunk filter;
};

/// Where a stored filter lies in the data file.
struct filter_location {
    uint32_t height;
    hash_digest block;
    uint64_t offset;
    uint32_t length;
};

class KND_API filter_store {
public:
    /// The block hash of the header chain at a height, if the chain reaches it.
    using block_lookup = std::function<std::optional<hash_digest>(uint32_t)>;

    static constexpr size_t preamble_size = 40;
    static constexpr size_t record_size = 68;

    /// Opens the store of `filter_type` in `directory`, creating it when it
    /// is not there. Keeps the records whose block hashes are the header
    /// chain's (`block_at`) and whose filters are whole, and checks the last
    /// sixteen filters against their headers (a torn write lands there).
    /// file_system when a file cannot be created, read or cut.
    [[nodiscard]]
    static std::expected<filter_store, std::error_code> open(std::filesystem::path const& directory,
        uint8_t filter_type, block_lookup const& block_at);

    [[nodiscard]] uint8_t type() const { return type_; }

    /// The first height the store holds, or would hold.
    [[nodiscard]] uint32_t first() const { return first_; }
    [[nodiscard]] size_t size() const { return headers_.size(); }
    [[nodiscard]] bool empty() const { return headers_.empty(); }

    /// The height of the next filter to append.
    [[nodiscard]] uint32_t next() const { return first_ + uint32_t(headers_.size()); }

    /// The highest height with a filter.
    [[nodiscard]] std::optional<uint32_t> tip() const;

    /// The filter header of the block before the first one.
    [[nodiscard]] hash_digest const& previous_header() const { return previous_; }

    /// The filter header of the tip, or the one before the first block when
    /// the store is empty: what the next filter header chains to.
    [[nodiscard]] hash_digest const& tip_header() const;

    [[nodiscard]] std::optional<hash_digest> header(uint32_t height) const;
    [[nodiscard]] std::optional<hash_digest> block(uint32_t height) const;

    /// Empties the store and starts it at `first`, after the block whose
    /// filter header is `previous`.
    [[nodiscard]] std::error_code restart(uint32_t first, hash_digest const& previous);

    /// Appends filters for the heights from next() on, already checked by the caller.
    [[nodiscard]] std::error_code append(std::span<filter_entry const> entries);

    /// Keeps the heights below `height`.
    [[nodiscard]] std::error_code truncate(uint32_t height);

    /// A stored filter, checked against its filter header. not_found outside
    /// the store, file_system when it cannot be read, bad_stream when it does
    /// not hash to its header.
    [[nodiscard]] std::expected<data_chunk, std::error_code> read(uint32_t height) const;

    /// The locations of the filters of heights [from, to) the store holds.
    [[nodiscard]] std::vector<filter_location> locations(uint32_t from, uint32_t to) const;

    [[nodiscard]] std::filesystem::path const& data_path() const { return data_path_; }

    /// Bytes of filters stored.
    [[nodiscard]] uint64_t data_bytes() const;

private:
    filter_store() = default;

    [[nodiscard]] std::error_code write_preamble() const;
    [[nodiscard]] std::expected<data_chunk, std::error_code> read_unchecked(size_t at) const;

    std::filesystem::path data_path_;
    std::filesystem::path index_path_;
    uint8_t type_ = 0;
    uint32_t first_ = 0;
    hash_digest previous_{};
    std::vector<hash_digest> blocks_;
    std::vector<hash_digest> headers_;
    std::vector<uint64_t> offsets_;
    std::vector<uint32_t> lengths_;
};

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_FILTER_STORE_HPP
