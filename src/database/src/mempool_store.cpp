// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/mempool_store.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

#include <spdlog/spdlog.h>

namespace kth::database {

using namespace kth::domain::chain;

namespace {
// Bumped whenever the on-disk layout changes; a mismatch is ignored on load.
constexpr uint64_t mempool_dump_version = 1;
} // namespace

bool store_mempool(std::filesystem::path const& file, std::vector<mempool_stored_tx> txs) {
    // Order by time_seen so parents (first-seen earlier) precede their children
    // on reload and re-admit cleanly.
    std::sort(txs.begin(), txs.end(),
        [](mempool_stored_tx const& a, mempool_stored_tx const& b) {
            return a.time_seen < b.time_seen;
        });

    // Exact buffer size: version + count, then per tx (serialized tx + time).
    size_t total = sizeof(uint64_t) * 2;
    for (auto const& e : txs) {
        total += e.tx->serialized_size(true) + sizeof(uint64_t);
    }

    data_chunk buffer(total);
    byte_writer writer(buffer);
    if ( ! writer.write_little_endian<uint64_t>(mempool_dump_version)) return false;
    if ( ! writer.write_little_endian<uint64_t>(static_cast<uint64_t>(txs.size()))) return false;
    for (auto const& e : txs) {
        if ( ! e.tx->to_data(writer, true)) return false;
        if ( ! writer.write_little_endian<uint64_t>(e.time_seen)) return false;
    }

    // Write to a sibling temp then rename over, so a crash mid-write never
    // leaves a truncated mempool.dat.
    auto const tmp = file.string() + ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if ( ! out) {
            spdlog::warn("[database] could not open {} for writing", tmp);
            return false;
        }
        out.write(reinterpret_cast<char const*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        out.flush();
        if ( ! out) {
            spdlog::warn("[database] failed to write {}", tmp);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        spdlog::warn("[database] failed to rename {} -> {}: {}", tmp, file.string(), ec.message());
        std::filesystem::remove(tmp, ec);
        return false;
    }

    spdlog::info("[database] dumped {} mempool transactions to {}", txs.size(), file.string());
    return true;
}

std::vector<mempool_stored_tx> load_mempool(std::filesystem::path const& file) {
    std::vector<mempool_stored_tx> result;

    std::error_code ec;
    if ( ! std::filesystem::exists(file, ec) || ec) {
        return result;  // no persisted mempool; not an error
    }

    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if ( ! in) {
        spdlog::warn("[database] could not open {} for reading", file.string());
        return result;
    }
    auto const size = static_cast<std::streamsize>(in.tellg());
    in.seekg(0);
    data_chunk buffer(static_cast<size_t>(std::max<std::streamsize>(size, 0)));
    if (size > 0 && ! in.read(reinterpret_cast<char*>(buffer.data()), size)) {
        spdlog::warn("[database] failed to read {}", file.string());
        return result;
    }

    byte_reader reader(buffer);
    auto const version = reader.read_little_endian<uint64_t>();
    if ( ! version || *version != mempool_dump_version) {
        spdlog::warn("[database] {}: missing or unsupported mempool dump version", file.string());
        return result;
    }
    auto const count = reader.read_little_endian<uint64_t>();
    if ( ! count) {
        return result;
    }

    result.reserve(static_cast<size_t>(*count));
    for (uint64_t i = 0; i < *count; ++i) {
        auto tx = transaction::from_data(reader, true);
        if ( ! tx) {
            spdlog::warn("[database] {}: corrupt transaction at {}/{}; stopping", file.string(), i, *count);
            break;
        }
        auto const time = reader.read_little_endian<uint64_t>();
        if ( ! time) {
            spdlog::warn("[database] {}: truncated after transaction {}; stopping", file.string(), i);
            break;
        }
        result.push_back({std::make_shared<domain::message::transaction>(std::move(*tx)), *time});
    }

    spdlog::info("[database] read {} mempool transactions from {}", result.size(), file.string());
    return result;
}

} // namespace kth::database
