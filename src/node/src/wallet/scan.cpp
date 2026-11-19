// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/wallet/scan.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <numeric>
#include <optional>
#include <thread>

#include <kth/domain/chain/compact_filter.hpp>
#include <kth/domain/chain/golomb_match.hpp>
#include <kth/domain/chain/input_prefix_filter.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth::node::wallet {

namespace {

// Filters are read in runs of up to this many bytes.
constexpr uint64_t run_bytes = 16 * 1024 * 1024;

// Positions of the entries active at a height: those whose start is at or
// below it, as a prefix of `order` (sorted by start).
using test_filter = std::function<std::vector<size_t>(filter_location const&, byte_span, std::span<size_t const>)>;

std::expected<scan_hits, std::error_code> scan_part(std::filesystem::path const& data,
        std::span<filter_location const> part, std::span<uint32_t const> starts,
        std::span<size_t const> order, test_filter const& test) {
    scan_hits hits;
    std::ifstream in(data, std::ios::binary);
    if ( ! in) return std::unexpected(error::file_system);
    data_chunk buffer;
    size_t i = 0;
    while (i < part.size()) {
        auto const base = part[i].offset;
        auto j = i;
        while (j + 1 < part.size() && part[j + 1].offset + part[j + 1].length - base <= run_bytes) {
            ++j;
        }
        buffer.resize(size_t(part[j].offset + part[j].length - base));
        in.seekg(std::streamoff(base));
        in.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
        if ( ! in) return std::unexpected(error::file_system);
        for (auto k = i; k <= j; ++k) {
            auto const& at = part[k];
            auto const active = size_t(std::ranges::upper_bound(starts, at.height) - starts.begin());
            if (active == 0) continue;
            auto const filter = byte_span(buffer.data() + (at.offset - base), at.length);
            auto matched = test(at, filter, order.first(active));
            if ( ! matched.empty()) {
                std::ranges::sort(matched);
                hits.emplace_back(at.height, std::move(matched));
            }
        }
        i = j + 1;
    }
    return hits;
}

std::expected<scan_hits, std::error_code> scan(std::filesystem::path const& data,
        std::span<filter_location const> locations, std::vector<uint32_t> const& from,
        size_t threads, test_filter const& test) {
    if (locations.empty() || from.empty()) return scan_hits{};
    std::vector<size_t> order(from.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::ranges::stable_sort(order, {}, [&](size_t k) { return from[k]; });
    std::vector<uint32_t> starts;
    starts.reserve(order.size());
    for (auto const k : order) starts.push_back(from[k]);

#if defined(__EMSCRIPTEN__)
    threads = 1;
#endif
    threads = std::clamp<size_t>(threads, 1, locations.size());
    auto const per_part = (locations.size() + threads - 1) / threads;
    std::vector<std::span<filter_location const>> parts;
    for (size_t at = 0; at < locations.size(); at += per_part) {
        parts.push_back(locations.subspan(at, std::min(per_part, locations.size() - at)));
    }
    std::vector<std::optional<std::expected<scan_hits, std::error_code>>> results(parts.size());
    if (parts.size() == 1) {
        results[0] = scan_part(data, parts[0], starts, order, test);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(parts.size());
        for (size_t p = 0; p < parts.size(); ++p) {
            workers.emplace_back([&, p] { results[p] = scan_part(data, parts[p], starts, order, test); });
        }
        for (auto& worker : workers) worker.join();
    }
    scan_hits hits;
    for (auto& result : results) {
        if ( ! *result) return std::unexpected(result->error());
        for (auto& hit : **result) hits.push_back(std::move(hit));
    }
    return hits;
}

} // namespace

std::vector<size_t> match_script_filter(hash_digest const& block, byte_span filter,
        std::span<hash_digest const> keys) {
    std::vector<size_t> all(keys.size());
    std::iota(all.begin(), all.end(), size_t{0});
    // The served bytes are the entry tree's root, then the set.
    if (filter.size() <= hash_size) return all;
    std::vector<byte_span> items(keys.begin(), keys.end());
    auto const found = domain::chain::golomb_match_each(domain::chain::compact_filter_key(block),
        filter.subspan(hash_size), items);
    if ( ! found) return all;
    return *found;
}

std::expected<scan_hits, std::error_code> scan_script_filters(std::filesystem::path const& data,
        std::span<filter_location const> locations, std::span<scan_key const> keys, size_t threads) {
    std::vector<uint32_t> from;
    for (auto const& key : keys) from.push_back(key.from);
    auto const test = [keys](filter_location const& at, byte_span filter, std::span<size_t const> active) {
        std::vector<hash_digest> asked;
        asked.reserve(active.size());
        for (auto const k : active) asked.push_back(keys[k].key);
        std::vector<size_t> matched;
        for (auto const position : match_script_filter(at.block, filter, asked)) {
            matched.push_back(active[position]);
        }
        return matched;
    };
    return scan(data, locations, from, threads, test);
}

std::expected<scan_hits, std::error_code> scan_input_prefix_filters(std::filesystem::path const& data,
        std::span<filter_location const> locations, std::span<scan_prefix const> prefixes, size_t threads) {
    std::vector<uint32_t> from;
    for (auto const& prefix : prefixes) from.push_back(prefix.from);
    auto const test = [prefixes](filter_location const&, byte_span filter, std::span<size_t const> active) {
        std::vector<size_t> matched;
        auto const parsed = domain::chain::parse_input_prefix_filter(filter);
        for (auto const k : active) {
            if ( ! parsed) {
                matched.push_back(k);
                continue;
            }
            auto const has = domain::chain::has_input_prefix(*parsed, prefixes[k].prefix, prefixes[k].bits);
            if ( ! has || *has) matched.push_back(k);
        }
        return matched;
    };
    return scan(data, locations, from, threads, test);
}

} // namespace kth::node::wallet
