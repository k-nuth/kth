// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/wallet/filter_store.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>

#include <kth/infrastructure/error.hpp>

namespace kth::node::wallet {

namespace {

// "klf" and the format version.
constexpr std::array<uint8_t, 4> preamble_magic{'k', 'l', 'f', 1};

// A torn write lands at the end: that many of the newest filters are checked
// against their headers when the store opens.
constexpr size_t checked_on_open = 16;

void put_u32(data_chunk& out, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        out.push_back(uint8_t(value >> (8 * i)));
    }
}

uint32_t get_u32(uint8_t const* in) {
    return uint32_t(in[0]) | uint32_t(in[1]) << 8 | uint32_t(in[2]) << 16 | uint32_t(in[3]) << 24;
}

hash_digest get_hash(uint8_t const* in) {
    hash_digest out{};
    std::copy_n(in, out.size(), out.begin());
    return out;
}

std::expected<data_chunk, std::error_code> read_file(std::filesystem::path const& path) {
    std::error_code ec;
    if ( ! std::filesystem::exists(path, ec)) {
        return data_chunk{};
    }
    std::ifstream in(path, std::ios::binary);
    if ( ! in) {
        return std::unexpected(error::file_system);
    }
    data_chunk bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return std::unexpected(error::file_system);
    }
    return bytes;
}

std::error_code resize(std::filesystem::path const& path, uint64_t size) {
    std::error_code ec;
    if ( ! std::filesystem::exists(path, ec)) {
        std::ofstream create(path, std::ios::binary);
        if ( ! create) return error::file_system;
    }
    std::filesystem::resize_file(path, size, ec);
    return ec ? std::error_code(error::file_system) : std::error_code{};
}

std::error_code append_file(std::filesystem::path const& path, data_chunk const& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if ( ! out) return error::file_system;
    out.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size()));
    out.flush();
    return out ? std::error_code{} : std::error_code(error::file_system);
}

} // namespace

hash_digest next_filter_header_from_hash(hash_digest const& filter_hash, hash_digest const& previous) {
    data_chunk pair(filter_hash.begin(), filter_hash.end());
    pair.insert(pair.end(), previous.begin(), previous.end());
    return bitcoin_hash(pair);
}

hash_digest next_filter_header(byte_span filter, hash_digest const& previous) {
    return next_filter_header_from_hash(bitcoin_hash(filter), previous);
}

std::expected<filter_store, std::error_code> filter_store::open(std::filesystem::path const& directory,
        uint8_t filter_type, block_lookup const& block_at) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return std::unexpected(error::file_system);

    filter_store store;
    store.type_ = filter_type;
    auto const name = "filters-" + std::to_string(unsigned(filter_type));
    store.data_path_ = directory / (name + ".dat");
    store.index_path_ = directory / (name + ".idx");

    auto const index = read_file(store.index_path_);
    if ( ! index) return std::unexpected(index.error());
    auto const& bytes = *index;

    // No store yet, or one of another format: start empty at genesis.
    if (bytes.size() < preamble_size || ! std::equal(preamble_magic.begin(), preamble_magic.end(), bytes.begin())) {
        if (auto const restarted = store.restart(0, null_hash)) return std::unexpected(restarted);
        return store;
    }
    store.first_ = get_u32(bytes.data() + 4);
    store.previous_ = get_hash(bytes.data() + 8);

    auto const data_size = std::filesystem::exists(store.data_path_, ec) ? std::filesystem::file_size(store.data_path_, ec) : 0;
    if (ec) return std::unexpected(error::file_system);

    uint64_t offset = 0;
    auto const records = (bytes.size() - preamble_size) / record_size;
    for (size_t i = 0; i < records; ++i) {
        auto const* record = bytes.data() + preamble_size + i * record_size;
        auto const block = get_hash(record);
        auto const length = get_u32(record + 64);
        if (block_at(store.first_ + uint32_t(i)) != block || offset + length > data_size) break;
        store.blocks_.push_back(block);
        store.headers_.push_back(get_hash(record + 32));
        store.offsets_.push_back(offset);
        store.lengths_.push_back(length);
        offset += length;
    }

    auto keep = store.size();
    for (auto at = keep - std::min(keep, checked_on_open); at < store.size(); ++at) {
        auto const filter = store.read_unchecked(at);
        auto const& previous = at == 0 ? store.previous_ : store.headers_[at - 1];
        if ( ! filter || next_filter_header(*filter, previous) != store.headers_[at]) {
            keep = at;
            break;
        }
    }
    if (auto const cut = store.truncate(store.first_ + uint32_t(keep))) return std::unexpected(cut);
    return store;
}

std::optional<uint32_t> filter_store::tip() const {
    if (headers_.empty()) return std::nullopt;
    return next() - 1;
}

hash_digest const& filter_store::tip_header() const {
    return headers_.empty() ? previous_ : headers_.back();
}

std::optional<hash_digest> filter_store::header(uint32_t height) const {
    if (height < first_ || height >= next()) return std::nullopt;
    return headers_[height - first_];
}

std::optional<hash_digest> filter_store::block(uint32_t height) const {
    if (height < first_ || height >= next()) return std::nullopt;
    return blocks_[height - first_];
}

std::error_code filter_store::write_preamble() const {
    data_chunk bytes(preamble_magic.begin(), preamble_magic.end());
    put_u32(bytes, first_);
    bytes.insert(bytes.end(), previous_.begin(), previous_.end());
    std::ofstream out(index_path_, std::ios::binary | std::ios::trunc);
    if ( ! out) return error::file_system;
    out.write(reinterpret_cast<char const*>(bytes.data()), std::streamsize(bytes.size()));
    out.flush();
    return out ? std::error_code{} : std::error_code(error::file_system);
}

std::error_code filter_store::restart(uint32_t first, hash_digest const& previous) {
    blocks_.clear();
    headers_.clear();
    offsets_.clear();
    lengths_.clear();
    first_ = first;
    previous_ = previous;
    if (auto const ec = resize(data_path_, 0)) return ec;
    return write_preamble();
}

std::error_code filter_store::append(std::span<filter_entry const> entries) {
    if (entries.empty()) return {};
    auto offset = offsets_.empty() ? uint64_t{0} : offsets_.back() + lengths_.back();
    data_chunk blob;
    data_chunk records;
    records.reserve(entries.size() * record_size);
    for (auto const& entry : entries) {
        blob.insert(blob.end(), entry.filter.begin(), entry.filter.end());
        records.insert(records.end(), entry.block.begin(), entry.block.end());
        records.insert(records.end(), entry.header.begin(), entry.header.end());
        put_u32(records, uint32_t(entry.filter.size()));
    }
    // The filters first: a record whose filter is missing is dropped on open.
    if (auto const ec = append_file(data_path_, blob)) return ec;
    if (auto const ec = append_file(index_path_, records)) return ec;
    for (auto const& entry : entries) {
        blocks_.push_back(entry.block);
        headers_.push_back(entry.header);
        offsets_.push_back(offset);
        lengths_.push_back(uint32_t(entry.filter.size()));
        offset += entry.filter.size();
    }
    return {};
}

std::error_code filter_store::truncate(uint32_t height) {
    auto const count = height <= first_ ? size_t{0} : std::min(size(), size_t(height - first_));
    auto const end = count == 0 ? uint64_t{0} : offsets_[count - 1] + lengths_[count - 1];
    blocks_.resize(count);
    headers_.resize(count);
    offsets_.resize(count);
    lengths_.resize(count);
    if (auto const ec = resize(index_path_, preamble_size + count * record_size)) return ec;
    return resize(data_path_, end);
}

std::expected<data_chunk, std::error_code> filter_store::read_unchecked(size_t at) const {
    std::ifstream in(data_path_, std::ios::binary);
    if ( ! in) return std::unexpected(error::file_system);
    data_chunk filter(lengths_[at]);
    in.seekg(std::streamoff(offsets_[at]));
    in.read(reinterpret_cast<char*>(filter.data()), std::streamsize(filter.size()));
    if ( ! in) return std::unexpected(error::file_system);
    return filter;
}

std::expected<data_chunk, std::error_code> filter_store::read(uint32_t height) const {
    if (height < first_ || height >= next()) return std::unexpected(error::not_found);
    auto const at = size_t(height - first_);
    auto filter = read_unchecked(at);
    if ( ! filter) return filter;
    auto const& previous = at == 0 ? previous_ : headers_[at - 1];
    if (next_filter_header(*filter, previous) != headers_[at]) return std::unexpected(error::bad_stream);
    return filter;
}

std::vector<filter_location> filter_store::locations(uint32_t from, uint32_t to) const {
    std::vector<filter_location> out;
    auto const begin = std::max(from, first_);
    auto const end = std::min(to, next());
    for (auto height = begin; height < end; ++height) {
        auto const at = size_t(height - first_);
        out.push_back(filter_location{height, blocks_[at], offsets_[at], lengths_[at]});
    }
    return out;
}

uint64_t filter_store::data_bytes() const {
    return offsets_.empty() ? 0 : offsets_.back() + lengths_.back();
}

} // namespace kth::node::wallet
