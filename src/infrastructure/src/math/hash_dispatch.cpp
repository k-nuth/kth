// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/math/hash_dispatch.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#if defined(KTH_SIGVERIFY_TURBO_BATCH)
#include <secp256k1_turbo/gpu_sha256.hpp>
#endif

#include <kth/infrastructure/math/hash_batch.hpp>

namespace kth::hash_dispatch {

namespace {

// ---------------------------------------------------------------------------
// The two places a batch can go
// ---------------------------------------------------------------------------

void hash_on_cpu(std::span<byte_span const> messages, std::span<hash_digest> out) {
    bitcoin_hash_batch(messages, out);
}

bool gpu_present() {
#if defined(KTH_SIGVERIFY_TURBO_BATCH)
    // min_batch() is zero when the device was turned off for hashing, which is
    // how a measurement keeps the device out of it.
    return secp256k1_turbo::gpu_sha256::available() && secp256k1_turbo::gpu_sha256::min_batch() != 0;
#else
    return false;
#endif
}

// False when the device did not do it: it is busy with other work, or failed.
bool hash_on_gpu(uint8_t const* const* messages, uint32_t const* sizes, size_t count, uint8_t* out) {
#if defined(KTH_SIGVERIFY_TURBO_BATCH)
    return secp256k1_turbo::gpu_sha256::sha256d_messages(out, messages, sizes, count);
#else
    (void)messages; (void)sizes; (void)count; (void)out;
    return false;
#endif
}

double elapsed_ns(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - since).count();
}

constexpr uint64_t blocks_of(uint64_t message_bytes) {
    // The message, the 0x80 byte and the eight bytes of length, in whole blocks.
    return (message_bytes + 9 + 63) / 64;
}

// ---------------------------------------------------------------------------
// Correcting a model
// ---------------------------------------------------------------------------

// How much of the way one batch moves the model, and the most a single batch
// is believed to differ from it.
constexpr double learning_rate = 0.1;
constexpr double largest_ratio = 4.0;

double correction(double predicted_ns, double measured_ns) {
    if (predicted_ns <= 0 || measured_ns <= 0) {
        return 1.0;
    }
    auto const ratio = std::clamp(measured_ns / predicted_ns, 1.0 / largest_ratio, largest_ratio);
    return 1.0 + learning_rate * (ratio - 1.0);
}

// Synthetic messages for the calibration: `count` of `size` bytes each, from
// one buffer of bytes that are not all equal.
struct synthetic {
    std::vector<uint8_t> bytes;
    std::vector<byte_span> messages;
    std::vector<hash_digest> out;

    synthetic(size_t count, size_t size, size_t one_long = 0) {
        bytes.resize(count * size + one_long);
        uint64_t x = 0x9e3779b97f4a7c15ULL;
        for (auto& b : bytes) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            b = uint8_t(x >> 24);
        }
        for (size_t i = 0; i < count; ++i) {
            messages.emplace_back(bytes.data() + i * size, size);
        }
        if (one_long != 0) {
            messages.emplace_back(bytes.data() + count * size, one_long);
        }
        out.resize(messages.size());
    }

    [[nodiscard]] uint64_t total() const { return bytes.size(); }
};

// The least of a few timings: what the work takes when nothing else is in
// the way.
template <typename Work>
double best_ns(Work&& work, int repetitions = 3) {
    double best = 0;
    for (int i = 0; i < repetitions; ++i) {
        auto const start = std::chrono::steady_clock::now();
        if ( ! work()) {
            return 0;
        }
        auto const took = elapsed_ns(start);
        best = (i == 0) ? took : std::min(best, took);
    }
    return best;
}

bool gpu_run(synthetic& s) {
    std::vector<uint8_t const*> pointers(s.messages.size());
    std::vector<uint32_t> sizes(s.messages.size());
    for (size_t i = 0; i < s.messages.size(); ++i) {
        pointers[i] = s.messages[i].data();
        sizes[i] = uint32_t(s.messages[i].size());
    }
    return hash_on_gpu(pointers.data(), sizes.data(), sizes.size(), s.out.front().data());
}

cost_model measure() {
    cost_model m;

    // The processor: the same number of messages at two lengths gives the
    // amount per byte, and what is left is the amount per message.
    {
        constexpr size_t count = 4096;
        synthetic small(count, 128);
        synthetic large(count, 1024);
        auto const t_small = best_ns([&] { hash_on_cpu(small.messages, small.out); return true; });
        auto const t_large = best_ns([&] { hash_on_cpu(large.messages, large.out); return true; });
        m.cpu_per_byte = std::max(0.0, (t_large - t_small) / double(large.total() - small.total()));
        m.cpu_per_message = std::max(0.0, (t_small - m.cpu_per_byte * double(small.total())) / double(count));
    }

    if ( ! gpu_present()) {
        return m;
    }

    // The device: few short messages, many short messages, and the few again
    // with one long message among them. The second against the first gives
    // the amount per byte; the third against the first, the amount per block
    // of the longest message; what is left of the first is the launch.
    constexpr size_t short_size = 256;
    constexpr size_t few = 2048;
    constexpr size_t many = 32768;
    constexpr size_t long_size = 256 * 1024;
    synthetic a(few, short_size);
    synthetic b(many, short_size);
    synthetic c(few, short_size, long_size);

    // The first launch pays for starting the device.
    if ( ! gpu_run(a)) {
        return m;
    }
    auto const t_a = best_ns([&] { return gpu_run(a); });
    auto const t_b = best_ns([&] { return gpu_run(b); });
    auto const t_c = best_ns([&] { return gpu_run(c); });
    if (t_a <= 0 || t_b <= 0 || t_c <= 0) {
        return m;       // the device was taken away meanwhile: processor only
    }
    m.gpu_per_byte = std::max(0.0, (t_b - t_a) / double(b.total() - a.total()));
    auto const long_blocks = double(blocks_of(long_size) - blocks_of(short_size));
    m.gpu_per_longest_block = std::max(0.0, (t_c - t_a - m.gpu_per_byte * double(long_size)) / long_blocks);
    m.gpu_per_launch = std::max(0.0, t_a - m.gpu_per_byte * double(a.total())
        - m.gpu_per_longest_block * double(blocks_of(short_size)));
    m.gpu = true;
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// cost_model
// ---------------------------------------------------------------------------

double cost_model::gpu_time(uint64_t bytes, uint64_t longest) const {
    return gpu_per_launch + gpu_per_byte * double(bytes) + gpu_per_longest_block * double(blocks_of(longest));
}

namespace {
constexpr std::string_view text_tag = "kth-hash-dispatch-1";
} // namespace

std::string cost_model::to_text() const {
    return fmt::format("{} {} {} {} {} {} {} {} {}", text_tag, cpu_per_message, cpu_per_byte, gpu ? 1 : 0,
        gpu_per_launch, gpu_per_byte, gpu_per_longest_block, cpu_batches, gpu_batches);
}

std::optional<cost_model> cost_model::from_text(std::string_view text) {
    auto const next = [&text]() -> std::string_view {
        while ( ! text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r')) {
            text.remove_prefix(1);
        }
        auto const end = std::min(text.find_first_of(" \r\n"), text.size());
        auto const field = text.substr(0, end);
        text.remove_prefix(end);
        return field;
    };
    auto const number = [&next](double& value) {
        auto const field = next();
        auto const [end, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
        return ec == std::errc{} && end == field.data() + field.size() && value >= 0 && value < 1e18;
    };

    if (next() != text_tag) {
        return std::nullopt;
    }
    cost_model m;
    double has_gpu = 0, cpu_seen = 0, gpu_seen = 0;
    if ( ! number(m.cpu_per_message) || ! number(m.cpu_per_byte) || ! number(has_gpu)
        || ! number(m.gpu_per_launch) || ! number(m.gpu_per_byte) || ! number(m.gpu_per_longest_block)
        || ! number(cpu_seen) || ! number(gpu_seen)) {
        return std::nullopt;
    }
    if (has_gpu != 0 && has_gpu != 1) {
        return std::nullopt;
    }
    m.gpu = has_gpu == 1;
    m.cpu_batches = uint64_t(cpu_seen);
    m.gpu_batches = uint64_t(gpu_seen);
    return m;
}

// ---------------------------------------------------------------------------
// The decision
// ---------------------------------------------------------------------------

plan choose(cost_model const& model, std::span<byte_span const> messages) {
    // The messages by length, in powers of two: bucket b holds those shorter
    // than 2^b and not in a bucket below. The cut between device and processor
    // is tried at every bucket boundary.
    constexpr size_t buckets = 34;
    std::array<uint64_t, buckets> count{}, bytes{}, longest{};
    uint64_t total_bytes = 0;
    for (auto const& message : messages) {
        // Above what the device takes in one message: never sent to it.
        auto const b = std::min<size_t>(std::bit_width(message.size()), buckets - 1);
        ++count[b];
        bytes[b] += message.size();
        longest[b] = std::max<uint64_t>(longest[b], message.size());
        total_bytes += message.size();
    }

    plan best;
    best.all_cpu_ns = model.cpu_time(messages.size(), total_bytes);
    best.planned_ns = best.all_cpu_ns;
    if ( ! model.gpu || messages.size() < minimum_gpu_messages) {
        return best;
    }

    uint64_t below_count = 0, below_bytes = 0, below_longest = 0;
    // Bucket 33 is everything of 2^32 bytes or more, which does not fit the
    // size the device is told: the cut stops before it.
    for (size_t b = 0; b + 1 < buckets; ++b) {
        if (count[b] == 0) continue;
        below_count += count[b];
        below_bytes += bytes[b];
        below_longest = std::max(below_longest, longest[b]);
        if (below_count < minimum_gpu_messages) continue;
        auto const time = model.gpu_time(below_bytes, below_longest)
            + model.cpu_time(messages.size() - below_count, total_bytes - below_bytes);
        if (time < best.planned_ns) {
            best.planned_ns = time;
            best.gpu_below = uint64_t(1) << b;
            best.gpu_messages = size_t(below_count);
            best.gpu_bytes = below_bytes;
            best.gpu_longest = below_longest;
        }
    }
    return best;
}

cost_model observe_cpu(cost_model model, uint64_t messages, uint64_t bytes, double measured_ns) {
    auto const scale = correction(model.cpu_time(messages, bytes), measured_ns);
    model.cpu_per_message *= scale;
    model.cpu_per_byte *= scale;
    ++model.cpu_batches;
    return model;
}

cost_model observe_gpu(cost_model model, uint64_t bytes, uint64_t longest, double measured_ns) {
    auto const scale = correction(model.gpu_time(bytes, longest), measured_ns);
    model.gpu_per_launch *= scale;
    model.gpu_per_byte *= scale;
    model.gpu_per_longest_block *= scale;
    ++model.gpu_batches;
    return model;
}

// ---------------------------------------------------------------------------
// The dispatcher
// ---------------------------------------------------------------------------

policy policy_from_environment() {
    // KTH_HASH_DISPATCH=cpu keeps every batch on the processor.
    if (char const* value = std::getenv("KTH_HASH_DISPATCH"); value != nullptr) {
        if (std::string_view(value) == "cpu") {
            return policy::cpu_only;
        }
    }
    return policy::automatic;
}

void dispatcher::set_model(cost_model const& value) {
    model = value;
    // A model from another run says what the device cost then; whether there
    // is one now is asked now.
    model.gpu = value.gpu && gpu_present();
    measured = true;
}

void dispatcher::calibrate() {
    model = measure();
    measured = true;
}

void dispatcher::hash(std::span<byte_span const> messages, std::span<hash_digest> out) {
    // Small batches never leave the processor, so they need no model: neither
    // a calibration nor a correction is worth more than hashing them.
    if (messages.size() < minimum_gpu_messages || how != policy::automatic || ! gpu_present()) {
        hash_on_cpu(messages, out);
        return;
    }
    if ( ! measured) {
        // The first batch large enough to matter measures the machine.
        calibrate();
    }

    auto const split = choose(model, messages);
    if (split.gpu_messages == 0) {
        uint64_t bytes = 0;
        for (auto const& message : messages) bytes += message.size();
        auto const start = std::chrono::steady_clock::now();
        hash_on_cpu(messages, out);
        model = observe_cpu(model, messages.size(), bytes, elapsed_ns(start));
        return;
    }

    // The short messages to the device, in the order they came; the rest to
    // the processor.
    std::vector<uint8_t const*> gpu_pointers;
    std::vector<uint32_t> gpu_sizes;
    std::vector<size_t> gpu_index;
    std::vector<byte_span> cpu_messages;
    std::vector<size_t> cpu_index;
    gpu_pointers.reserve(split.gpu_messages);
    gpu_sizes.reserve(split.gpu_messages);
    gpu_index.reserve(split.gpu_messages);
    cpu_messages.reserve(messages.size() - split.gpu_messages);
    cpu_index.reserve(messages.size() - split.gpu_messages);
    uint64_t cpu_bytes = 0;
    for (size_t i = 0; i < messages.size(); ++i) {
        auto const& message = messages[i];
        if (message.size() < split.gpu_below) {
            gpu_pointers.push_back(message.data());
            gpu_sizes.push_back(uint32_t(message.size()));
            gpu_index.push_back(i);
        } else {
            cpu_messages.push_back(message);
            cpu_index.push_back(i);
            cpu_bytes += message.size();
        }
    }

    std::vector<hash_digest> gpu_out(gpu_index.size());
    auto const gpu_start = std::chrono::steady_clock::now();
    bool const on_device = hash_on_gpu(gpu_pointers.data(), gpu_sizes.data(), gpu_sizes.size(),
        gpu_out.empty() ? nullptr : gpu_out.front().data());
    auto const gpu_took = elapsed_ns(gpu_start);

    if ( ! on_device) {
        // Busy with other work, or failed: the processor hashes everything,
        // and nothing is learned about the device from a launch it did not do.
        hash_on_cpu(messages, out);
        return;
    }
    for (size_t k = 0; k < gpu_index.size(); ++k) {
        out[gpu_index[k]] = gpu_out[k];
    }

    double cpu_took = 0;
    if ( ! cpu_messages.empty()) {
        std::vector<hash_digest> cpu_out(cpu_messages.size());
        auto const cpu_start = std::chrono::steady_clock::now();
        hash_on_cpu(cpu_messages, cpu_out);
        cpu_took = elapsed_ns(cpu_start);
        for (size_t k = 0; k < cpu_index.size(); ++k) {
            out[cpu_index[k]] = cpu_out[k];
        }
    }

    model = observe_gpu(model, split.gpu_bytes, split.gpu_longest, gpu_took);
    // A handful of long messages says little about the processor.
    if (cpu_messages.size() >= minimum_gpu_messages) {
        model = observe_cpu(model, cpu_messages.size(), cpu_bytes, cpu_took);
    }
}

} // namespace kth::hash_dispatch
