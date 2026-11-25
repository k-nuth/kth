// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/compact_filter_store.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <kth/infrastructure/utility/data.hpp>

#include <spdlog/spdlog.h>

namespace kth::node::detail {

namespace {

constexpr std::string_view magic = "KTHCFIDX";
constexpr size_t version_offset = magic.size();
constexpr size_t count_offset = version_offset + sizeof(uint32_t);
constexpr size_t tip_offset = count_offset + sizeof(uint32_t);

void put_uint32(uint8_t* out, uint32_t value) noexcept {
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        out[i] = uint8_t(value >> (8 * i));
    }
}

uint32_t get_uint32(uint8_t const* in) noexcept {
    uint32_t value = 0;
    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
        value |= uint32_t(in[i]) << (8 * i);
    }
    return value;
}

using file_header = std::array<uint8_t, compact_filter_store::file_header_size>;

file_header make_header(uint32_t count, hash_digest const& tip) noexcept {
    file_header header{};
    std::copy(magic.begin(), magic.end(), header.begin());
    put_uint32(header.data() + version_offset, compact_filter_store::format_version);
    put_uint32(header.data() + count_offset, count);
    std::copy(tip.begin(), tip.end(), header.begin() + tip_offset);
    return header;
}

bool write(std::fstream& file, size_t offset, uint8_t const* data, size_t size) {
    file.seekp(std::streamoff(offset));
    file.write(reinterpret_cast<char const*>(data), std::streamsize(size));
    file.flush();
    return bool(file);
}

} // namespace

hash_digest chain_filter_header(hash_digest const& filter_hash, hash_digest const& previous) noexcept {
    return bitcoin_hash(splice(filter_hash, previous));
}

compact_filter_store::compact_filter_store(kth::path file)
    : file_(std::move(file))
{}

void compact_filter_store::load() {
    records_.clear();
    tip_ = null_hash;
    saved_ = 0;
    counted_ = 0;
    tip_saved_ = true;

    std::ifstream file(file_, std::ios::binary);
    if ( ! file) {
        return;
    }
    auto const refuse = [this](char const* why) {
        spdlog::warn("[compact filters] {} {}; the filter headers are computed again", file_.string(), why);
    };
    // Read as one block of the size the file has: a read that fails leaves the
    // stream failed, where reading through the buffer's iterators would throw.
    std::error_code size_ec;
    auto const size = std::filesystem::file_size(file_, size_ec);
    if (size_ec) {
        refuse("cannot be read");
        return;
    }
    data_chunk bytes(size_t{size});
    file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if ( ! file) {
        refuse("cannot be read");
        return;
    }
    if (bytes.size() < file_header_size
        || ! std::equal(magic.begin(), magic.end(), bytes.begin())
        || get_uint32(bytes.data() + version_offset) != format_version) {
        refuse("is not a filter header file of this version");
        return;
    }
    auto const count = size_t(get_uint32(bytes.data() + count_offset));
    if ((bytes.size() - file_header_size) / record_size < count) {
        refuse("is shorter than the records it counts");
        return;
    }

    std::vector<compact_filter_record> records(count);
    hash_digest previous = null_hash;
    for (size_t i = 0; i < count; ++i) {
        auto const at = bytes.data() + file_header_size + i * record_size;
        std::copy_n(at, hash_size, records[i].filter_hash.begin());
        std::copy_n(at + hash_size, hash_size, records[i].filter_header.begin());
        if (chain_filter_header(records[i].filter_hash, previous) != records[i].filter_header) {
            refuse("holds a record that does not chain to the one below it");
            return;
        }
        previous = records[i].filter_header;
    }
    if (count == 0) {
        return;
    }
    records_ = std::move(records);
    std::copy_n(bytes.data() + tip_offset, hash_size, tip_.begin());
    saved_ = count;
    counted_ = count;
}

hash_digest compact_filter_store::header_before(size_t height) const {
    return height == 0 ? null_hash : records_[height - 1].filter_header;
}

void compact_filter_store::append(hash_digest const& filter_hash) {
    records_.push_back({filter_hash, chain_filter_header(filter_hash, header_before(records_.size()))});
    tip_saved_ = false;
}

void compact_filter_store::truncate(size_t count) {
    if (count >= records_.size()) {
        return;
    }
    records_.resize(count);
    saved_ = std::min(saved_, count);
    tip_saved_ = false;
}

namespace {

code failed(kth::path const& file, char const* what) {
    spdlog::warn("[compact filters] could not {} {}", what, file.string());
    return error::operation_failed;
}

data_chunk serialized(std::vector<compact_filter_record> const& records, size_t from) {
    data_chunk bytes;
    bytes.reserve((records.size() - from) * compact_filter_store::record_size);
    for (auto i = from; i < records.size(); ++i) {
        bytes.insert(bytes.end(), records[i].filter_hash.begin(), records[i].filter_hash.end());
        bytes.insert(bytes.end(), records[i].filter_header.begin(), records[i].filter_header.end());
    }
    return bytes;
}

} // namespace

code compact_filter_store::save(hash_digest const& tip) {
    auto const held = records_.empty() ? null_hash : tip;
    // Records the file counts would change: the file is replaced whole.
    auto const result = saved_ < counted_ || saved_ == 0 ? save_whole(held) : save_added(held);
    if (result) {
        // How far the failed save got is not known, so nothing of the file is
        // counted on: the next save replaces it whole.
        saved_ = 0;
        return result;
    }
    tip_ = held;
    saved_ = records_.size();
    counted_ = records_.size();
    tip_saved_ = true;
    return error::success;
}

code compact_filter_store::save_whole(hash_digest const& tip) {
    auto replacement = file_;
    replacement += ".new";
    {
        std::fstream file(replacement, std::ios::binary | std::ios::out | std::ios::trunc);
        if ( ! file.is_open()) {
            return failed(replacement, "create");
        }
        auto const header = make_header(uint32_t(records_.size()), tip);
        auto const bytes = serialized(records_, 0);
        if ( ! write(file, 0, header.data(), header.size())
            || ! write(file, file_header_size, bytes.data(), bytes.size())) {
            return failed(replacement, "write");
        }
        file.close();
        if ( ! file) {
            return failed(replacement, "write");
        }
    }
    std::error_code rename_ec;
    std::filesystem::rename(replacement, file_, rename_ec);
    if (rename_ec) {
        return failed(file_, "replace");
    }
    return error::success;
}

code compact_filter_store::save_added(hash_digest const& tip) {
    std::fstream file(file_, std::ios::binary | std::ios::in | std::ios::out);
    if ( ! file.is_open()) {
        return failed(file_, "open");
    }
    // The records first: until the count below is written, the file is the
    // one the save before left.
    if (saved_ < records_.size()) {
        auto const bytes = serialized(records_, saved_);
        if ( ! write(file, file_header_size + saved_ * record_size, bytes.data(), bytes.size())) {
            return failed(file_, "write");
        }
    }
    auto const header = make_header(uint32_t(records_.size()), tip);
    if ( ! write(file, 0, header.data(), header.size())) {
        return failed(file_, "write");
    }
    file.close();
    if ( ! file) {
        return failed(file_, "write");
    }
    // What a save cut short left past the count is dropped. The count already
    // ignores it, so a failure here costs nothing.
    std::error_code ignored;
    std::filesystem::resize_file(file_, file_header_size + records_.size() * record_size, ignored);
    return error::success;
}

} // namespace kth::node::detail
