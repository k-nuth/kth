// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// INTERNAL. Not installed, not exported, no ABI.

#ifndef KTH_NODE_DETAIL_COMPACT_FILTER_STORE_HPP
#define KTH_NODE_DETAIL_COMPACT_FILTER_STORE_HPP

#if ! defined(KTH_WITH_NEUTRINO)
#error "Compact filters are an optional feature: build with the Conan option neutrino=True."
#endif

#include <cstddef>
#include <cstdint>
#include <vector>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/path.hpp>
#include <kth/node/define.hpp>

namespace kth::node::detail {

// What the node keeps of the regular compact filter of one block: the hash of
// the filter and the filter header, which chains that hash to the filter
// header of the block before (all zeros before the first block).
struct compact_filter_record {
    hash_digest filter_hash{null_hash};
    hash_digest filter_header{null_hash};

    [[nodiscard]]
    friend bool operator==(compact_filter_record const&, compact_filter_record const&) = default;
};

// The filter header of a block: the double SHA-256 of its filter hash and the
// filter header of the block before.
[[nodiscard]] KND_API
hash_digest chain_filter_header(hash_digest const& filter_hash, hash_digest const& previous) noexcept;

// The records of one chain, lowest first: record `h` is the block at height
// `h`. Held whole in memory, 64 bytes a block, and in one file:
//
//     "KTHCFIDX"  version  count  tip      then `count` records
//      8 bytes    4        4      32       64 bytes each
//
// `tip` is the hash of the block the last record belongs to: the records say
// nothing about their blocks, so this is what ties the file to a chain.
//
// A save that only adds records writes them and then the count and the tip. One
// cut short before that last write leaves the count of the save before it, and
// whatever the file holds past its count is ignored. One cut short in the
// middle of it can leave a count and a tip that are not of the same save: the
// records still chain, so the load takes them, and it is for the owner to find
// that the block named is not at the height of the last record and to compute
// them again. A save that changes records the file already counts, as after a
// reorganization, writes a whole file beside it and moves it over, so the file
// is the old one or the new one and never a mix of both. So does the save that
// follows one that failed: what a failed save left in the file is not known.
//
// The filter headers chain, so a load checks every record against the one
// below it, and a file that fails the check, or is not this format, is taken
// as holding nothing: its records are computed again.
//
// One owner: no call is safe from two tasks at once.
struct KND_API compact_filter_store {
    static constexpr uint32_t format_version = 1;
    static constexpr size_t file_header_size = 8 + 4 + 4 + hash_size;
    static constexpr size_t record_size = 2 * hash_size;

    explicit compact_filter_store(kth::path file);

    // Reads the file. What was held before is dropped.
    void load();

    // The number of records held: the blocks at heights below it.
    [[nodiscard]] size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] bool empty() const noexcept { return records_.empty(); }

    // The block the last load or save named as that of the last record: all
    // zeros when it held nothing. Not of the records held now: an append or a
    // truncate leaves it as it was until the next save.
    [[nodiscard]] hash_digest const& tip() const noexcept { return tip_; }

    [[nodiscard]] compact_filter_record const& at(size_t height) const { return records_[height]; }

    // The filter header of the block before `height`: all zeros for the first
    // block.
    [[nodiscard]] hash_digest header_before(size_t height) const;

    // Adds the record of the next block, chained to the last one.
    void append(hash_digest const& filter_hash);

    // Keeps the first `count` records.
    void truncate(size_t count);

    // Whether the file holds what the store does.
    [[nodiscard]] bool saved() const noexcept { return saved_ == records_.size() && tip_saved_; }

    // Writes what the file does not hold, naming `tip` as the block of the
    // last record.
    [[nodiscard]] code save(hash_digest const& tip);

private:
    kth::path const file_;
    std::vector<compact_filter_record> records_;
    hash_digest tip_{null_hash};
    [[nodiscard]] code save_whole(hash_digest const& tip);
    [[nodiscard]] code save_added(hash_digest const& tip);

    // The leading records the file holds as the store does.
    size_t saved_{0};
    // The records the file counts.
    size_t counted_{0};
    bool tip_saved_{true};
};

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_COMPACT_FILTER_STORE_HPP
