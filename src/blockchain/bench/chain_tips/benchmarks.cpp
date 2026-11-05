// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Build kth_blockchain_bench_chain_tips in Release, then run:
//   kth_blockchain_bench_chain_tips 1000000 16 31 mixed
// Arguments: total headers (including genesis), returned tips, samples,
//            uniform|mixed|late. Run each case in a separate process for RSS.
// Synthetic headers, insertion, warmup and oracle checks are outside timing.
// RSS is a process high-water mark INCLUDING the index, not scratch allocation.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

#include <kth/database/header_index.hpp>
#include <kth/domain/chain/block_ticks.hpp>
#include <kth/domain/chain/faster_blocks.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>

namespace {
using kth::hash_digest;
using kth::database::header_index;
constexpr uint32_t base_time = 1'600'000'000;
constexpr size_t branch_depth = 64;

size_t parse_count(std::string_view text) {
    size_t value = 0;
    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0)
        throw std::invalid_argument("arguments must be positive integers");
    return value;
}

std::string peak_rss_kib() {
#if defined(__linux__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__)
        return std::to_string(usage.ru_maxrss / 1024);
#else
        return std::to_string(usage.ru_maxrss);
#endif
    }
#endif
    return "NA";
}

template <typename Index>
auto query(Index const& index, hash_digest const& tip, uint64_t activation) {
    // Also build this exact benchmark against the pre-change implementation
    // for comparison. The CSV identifies whether tick metadata is available.
    if constexpr (requires { index.get_chain_tips(tip, activation); })
        return index.get_chain_tips(tip, activation);
    else
        return index.get_chain_tips(tip);
}

template <typename Tip>
constexpr bool has_ticks = requires(Tip const& tip) { tip.ticks; };

template <typename Tip>
uint64_t consume(Tip const& tip) {
    uint64_t result = tip.height + tip.branch_length + uint64_t(tip.status) + tip.hash[0];
    if constexpr (has_ticks<Tip>) result += tip.ticks;
    return result;
}

template <typename Tip>
void verify(Tip const& tip, header_index const& index, uint64_t activation) {
    if constexpr (has_ticks<Tip>) {
        auto const entry = index.find(tip.hash);
        auto timestamp = [&](size_t height) -> std::optional<uint32_t> {
            auto const ancestor = index.get_ancestor(entry, int32_t(height));
            if (ancestor == header_index::null_index) return std::nullopt;
            return index.get_timestamp(ancestor);
        };
        // Independent sorted-MTP discovery, not the query's rolling bit window.
        auto const ha = kth::domain::chain::find_faster_blocks_activation_height(
            tip.height, activation, timestamp);
        auto const expected = ha ? kth::domain::chain::block_ticks::activated_at(*ha)
                                 : kth::domain::chain::block_ticks{};
        if (tip.ticks != expected.height_to_tick(tip.height))
            throw std::runtime_error("tick metadata differs from the branch MTP oracle");
    }
}
} // namespace

int main(int argc, char** argv) try {
    if (argc > 5) throw std::invalid_argument("usage: benchmark [headers] [tips] [samples] [uniform|mixed|late]");
    auto const headers = argc > 1 ? parse_count(argv[1]) : 1'000'000;
    auto const tips = argc > 2 ? parse_count(argv[2]) : 16;
    auto const samples = argc > 3 ? parse_count(argv[3]) : 31;
    std::string_view const mode = argc > 4 ? argv[4] : "mixed";
    if (mode != "uniform" && mode != "mixed" && mode != "late")
        throw std::invalid_argument("mode must be uniform, mixed or late");
    if (headers > (std::numeric_limits<uint32_t>::max() - base_time - 7 * 60) / 60 ||
        headers <= 128 || tips - 1 > (headers - 129) / branch_depth)
        throw std::invalid_argument("fixture needs at least 129 trunk headers and 64 per side branch");
    if (samples < 3 || samples > 10000)
        throw std::invalid_argument("sample count must be between 3 and 10000");

    auto const height = headers - (tips - 1) * branch_depth - 1;
    auto const fork_height = height - branch_depth;
    auto const ha = mode == "late" ? height - branch_depth / 2 : height / 2;
    uint64_t const activation = mode == "uniform" ? std::numeric_limits<uint64_t>::max()
        : uint64_t(base_time) + (ha - 5) * 60;
    header_index index(headers);
    uint32_t serial = 0;
    auto add = [&](hash_digest const& parent, size_t h, uint32_t offset) {
        hash_digest merkle{};
        for (size_t b = 0; b != 4; ++b) merkle[b] = uint8_t(serial >> (8 * b));
        kth::domain::chain::header const header{4, parent, merkle,
            uint32_t(uint64_t(base_time) + h * 60 + offset), 0x207fffff, serial++};
        // Hash the wire bytes directly: header hash diagnostics retain every
        // fixture hash and would otherwise pollute the process RSS measurement.
        std::array<uint8_t, 80> raw{};
        kth::byte_writer writer(raw);
        if (!header.to_data(writer, true)) throw std::runtime_error("header serialization failed");
        auto const hash = kth::bitcoin_hash(raw);
        if (!index.add(hash, header).inserted) throw std::runtime_error("duplicate fixture header");
        return hash;
    };
    auto previous = add(kth::null_hash, 0, 0);
    hash_digest fork{};
    for (size_t h = 1; h <= height; ++h) {
        previous = add(previous, h, 0);
        if (h == fork_height) fork = previous;
    }
    auto const connected = previous;
    for (size_t t = 1; t < tips; ++t) {
        previous = fork;
        for (size_t h = fork_height + 1; h <= height; ++h)
            previous = add(previous, h, uint32_t(t % 7) * 60);
    }
    if (index.size() != headers) throw std::runtime_error("incorrect fixture size");

    auto const rss_before = peak_rss_kib();
    auto warmup = query(index, connected, activation);
    if (!warmup || warmup->size() != tips) throw std::runtime_error("incorrect tip count");
    for (auto const& tip : *warmup) {
        if (tip.height != height || tip.branch_length != (tip.hash == connected ? 0 : branch_depth))
            throw std::runtime_error("incorrect tip ancestry metadata");
        verify(tip, index, activation);
    }
    warmup.reset();
    uint64_t checksum = 0;
    std::vector<double> milliseconds;
    milliseconds.reserve(samples);
    for (size_t i = 0; i < samples; ++i) {
        auto const start = std::chrono::steady_clock::now();
        auto result = query(index, connected, activation);
        auto const end = std::chrono::steady_clock::now();
        if (!result || result->size() != tips) throw std::runtime_error("query lost tips");
        milliseconds.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        for (auto const& tip : *result) checksum += consume(tip);
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    std::cout << "headers,tips,connected_height,side_branch_depth,samples,mode,tick_metadata,min_ms,median_ms,p95_ms,peak_rss_before_queries_kib,peak_rss_after_queries_kib,checksum\n"
        << headers << ',' << tips << ',' << height << ',' << branch_depth << ',' << samples << ',' << mode << ','
        << has_ticks<header_index::chain_tip> << ',' << milliseconds.front() << ','
        << milliseconds[samples / 2] << ',' << milliseconds[(samples * 95 + 99) / 100 - 1] << ','
        << rss_before << ',' << peak_rss_kib() << ',' << checksum << '\n';
    return 0;
} catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
