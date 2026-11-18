// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <catch2/catch_approx.hpp>

#include <cstdint>
#include <vector>

#include <kth/infrastructure/math/hash_dispatch.hpp>

using namespace kth;
using namespace kth::hash_dispatch;

namespace {

// A machine shaped like the one the numbers were first taken on: a processor
// at about 200 ns for a 500-byte message, and a device that costs a third of
// a millisecond to launch, little per byte, and two microseconds per block of
// its longest message.
cost_model sample_model() {
    cost_model m;
    m.cpu_per_message = 60;
    m.cpu_per_byte = 0.3;
    m.gpu = true;
    m.gpu_per_launch = 300'000;
    m.gpu_per_byte = 0.07;
    m.gpu_per_longest_block = 2'000;
    return m;
}

// `count` messages of `size` bytes. choose() reads lengths only, so they all
// point at the same bytes.
void add(std::vector<byte_span>& messages, std::vector<uint8_t> const& bytes, size_t count, size_t size) {
    REQUIRE(size <= bytes.size());
    for (size_t i = 0; i < count; ++i) messages.emplace_back(bytes.data(), size);
}

std::vector<uint8_t> const backing(300'000, 0x5a);

} // namespace

TEST_CASE("hash dispatch: without a device everything stays on the processor", "[hash_dispatch]") {
    auto model = sample_model();
    model.gpu = false;
    std::vector<byte_span> messages;
    add(messages, backing, 100'000, 400);
    auto const split = choose(model, messages);
    CHECK(split.gpu_messages == 0u);
    CHECK(split.gpu_below == 0u);
    CHECK(split.planned_ns == split.all_cpu_ns);
}

TEST_CASE("hash dispatch: a batch too small for a launch stays on the processor", "[hash_dispatch]") {
    std::vector<byte_span> messages;
    add(messages, backing, minimum_gpu_messages - 1, 400);
    auto const split = choose(sample_model(), messages);
    CHECK(split.gpu_messages == 0u);
}

TEST_CASE("hash dispatch: a batch the launch does not pay for stays on the processor", "[hash_dispatch]") {
    // 300 messages cost the processor 54 microseconds; a launch alone is 300.
    std::vector<byte_span> messages;
    add(messages, backing, 300, 400);
    auto const split = choose(sample_model(), messages);
    CHECK(split.gpu_messages == 0u);
    CHECK(split.planned_ns == split.all_cpu_ns);
}

TEST_CASE("hash dispatch: many short messages go to the device", "[hash_dispatch]") {
    std::vector<byte_span> messages;
    add(messages, backing, 50'000, 400);
    auto const split = choose(sample_model(), messages);
    CHECK(split.gpu_messages == 50'000u);
    CHECK(split.gpu_bytes == 50'000u * 400u);
    CHECK(split.gpu_longest == 400u);
    CHECK(split.gpu_below == 512u);         // the first power of two above 400
    CHECK(split.planned_ns < split.all_cpu_ns);
}

TEST_CASE("hash dispatch: long messages among short ones stay on the processor", "[hash_dispatch]") {
    // One 250,000-byte message would hold a launch for eight milliseconds,
    // which the processor needs 75 microseconds for.
    std::vector<byte_span> messages;
    add(messages, backing, 50'000, 400);
    add(messages, backing, 3, 250'000);
    auto const split = choose(sample_model(), messages);
    CHECK(split.gpu_messages == 50'000u);
    CHECK(split.gpu_longest == 400u);
    CHECK(split.gpu_below <= 250'000u);

    // The plan is the better of the two it had to choose between.
    auto const model = sample_model();
    auto const all_on_device = model.gpu_time(50'000u * 400u + 3u * 250'000u, 250'000);
    CHECK(split.planned_ns < all_on_device);
    CHECK(split.planned_ns < split.all_cpu_ns);
}

TEST_CASE("hash dispatch: the cut falls where the device stops paying", "[hash_dispatch]") {
    // Short, medium and long messages. The medium ones are few and each adds
    // blocks to the longest message of the launch.
    std::vector<byte_span> messages;
    add(messages, backing, 40'000, 300);
    add(messages, backing, 2'000, 3'000);
    add(messages, backing, 20, 100'000);
    auto const model = sample_model();
    auto const split = choose(model, messages);

    // Whatever it chose, no cut at a power of two does better.
    for (uint64_t cut = 1; cut <= (uint64_t(1) << 32); cut <<= 1) {
        uint64_t n = 0, bytes = 0, longest = 0, rest = 0, rest_bytes = 0;
        for (auto const& m : messages) {
            if (m.size() < cut) { ++n; bytes += m.size(); longest = std::max<uint64_t>(longest, m.size()); }
            else { ++rest; rest_bytes += m.size(); }
        }
        if (n < minimum_gpu_messages) continue;
        CHECK(split.planned_ns <= model.gpu_time(bytes, longest) + model.cpu_time(rest, rest_bytes) + 1e-6);
    }
    CHECK(split.gpu_messages >= 40'000u);
    CHECK(split.gpu_below <= 100'000u);
}

TEST_CASE("hash dispatch: a real batch moves the model part of the way", "[hash_dispatch]") {
    auto const model = sample_model();
    auto const predicted = model.cpu_time(10'000, 4'000'000);

    // Twice as slow as predicted: a tenth of the way there.
    auto const slower = observe_cpu(model, 10'000, 4'000'000, 2 * predicted);
    CHECK(slower.cpu_time(10'000, 4'000'000) == Catch::Approx(1.1 * predicted));
    CHECK(slower.cpu_batches == 1u);
    // The device's side is not touched.
    CHECK(slower.gpu_per_launch == model.gpu_per_launch);

    // As predicted: nothing changes but the count.
    auto const same = observe_cpu(model, 10'000, 4'000'000, predicted);
    CHECK(same.cpu_per_byte == Catch::Approx(model.cpu_per_byte));

    // A hundred times slower is not believed whole: it counts as four times.
    auto const absurd = observe_cpu(model, 10'000, 4'000'000, 100 * predicted);
    CHECK(absurd.cpu_time(10'000, 4'000'000) == Catch::Approx(1.3 * predicted));

    auto const gpu_predicted = model.gpu_time(4'000'000, 400);
    auto const faster = observe_gpu(model, 4'000'000, 400, gpu_predicted / 2);
    CHECK(faster.gpu_time(4'000'000, 400) == Catch::Approx(0.95 * gpu_predicted));
    CHECK(faster.gpu_batches == 1u);
    CHECK(faster.cpu_per_byte == model.cpu_per_byte);
}

TEST_CASE("hash dispatch: repeated batches bring the model to what the machine does", "[hash_dispatch]") {
    auto model = sample_model();
    auto const before = model.gpu_time(4'000'000, 400);
    // The device became three times slower, say because other work shares it.
    for (int i = 0; i < 60; ++i) {
        model = observe_gpu(model, 4'000'000, 400, 3 * before);
    }
    CHECK(model.gpu_time(4'000'000, 400) == Catch::Approx(3 * before).epsilon(0.02));

    // And the decision follows: what went to the device now stays.
    std::vector<byte_span> messages;
    add(messages, backing, 3'000, 400);
    CHECK(choose(sample_model(), messages).gpu_messages == 3'000u);
    CHECK(choose(model, messages).gpu_messages == 0u);
}

TEST_CASE("hash dispatch: a model survives being written down", "[hash_dispatch]") {
    auto model = sample_model();
    model.cpu_per_byte = 0.31415926535;
    model.cpu_batches = 12345;
    model.gpu_batches = 678;
    auto const read = cost_model::from_text(model.to_text());
    REQUIRE(read);
    CHECK(read->cpu_per_message == Catch::Approx(model.cpu_per_message));
    CHECK(read->cpu_per_byte == Catch::Approx(model.cpu_per_byte));
    CHECK(read->gpu == model.gpu);
    CHECK(read->gpu_per_launch == Catch::Approx(model.gpu_per_launch));
    CHECK(read->gpu_per_byte == Catch::Approx(model.gpu_per_byte));
    CHECK(read->gpu_per_longest_block == Catch::Approx(model.gpu_per_longest_block));
    CHECK(read->cpu_batches == 12345u);
    CHECK(read->gpu_batches == 678u);

    // With a line break after it, as in a file.
    CHECK(cost_model::from_text(model.to_text() + "\n"));
}

TEST_CASE("hash dispatch: text that is not a model is refused", "[hash_dispatch]") {
    CHECK_FALSE(cost_model::from_text(""));
    CHECK_FALSE(cost_model::from_text("something else entirely"));
    CHECK_FALSE(cost_model::from_text("kth-hash-dispatch-0 1 2 1 3 4 5 6 7"));     // another version
    CHECK_FALSE(cost_model::from_text("kth-hash-dispatch-1 1 2 1 3 4"));           // cut short
    CHECK_FALSE(cost_model::from_text("kth-hash-dispatch-1 1 2 1 3 4 x 6 7"));     // not a number
    CHECK_FALSE(cost_model::from_text("kth-hash-dispatch-1 1 2 7 3 4 5 6 7"));     // device neither there nor not
    CHECK_FALSE(cost_model::from_text("kth-hash-dispatch-1 -1 2 1 3 4 5 6 7"));    // a negative cost
}

TEST_CASE("hash dispatch: hashing through the controller gives the same hashes", "[hash_dispatch]") {
    // Whatever this machine has, and wherever the batch is sent, the hashes
    // are those of bitcoin_hash. Short and long messages together, enough of
    // them for a launch where there is a device.
    std::vector<uint8_t> bytes(600'000);
    uint64_t x = 88172645463325252ULL;
    for (auto& b : bytes) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; b = uint8_t(x >> 24); }
    std::vector<byte_span> messages;
    for (size_t i = 0; i < 6000; ++i) {
        auto const size = 60 + (i * 7919) % 900;
        messages.emplace_back(bytes.data() + (i * 31) % 1000, size);
    }
    messages.emplace_back(bytes.data(), 250'000);
    messages.emplace_back(bytes.data() + 5, 0);
    messages.emplace_back(bytes.data() + 17, 100'001);

    for (int round = 0; round < 3; ++round) {
        std::vector<hash_digest> out(messages.size(), hash_digest{0x33});
        hash(messages, out);
        for (size_t i = 0; i < messages.size(); ++i) {
            INFO("round " << round << ", message " << i << " of " << messages[i].size() << " bytes");
            REQUIRE(out[i] == bitcoin_hash(messages[i]));
        }
    }

    // And with the device ruled out.
    auto const before = current_policy();
    set_policy(policy::cpu_only);
    std::vector<hash_digest> out(messages.size());
    hash(messages, out);
    for (size_t i = 0; i < messages.size(); ++i) {
        REQUIRE(out[i] == bitcoin_hash(messages[i]));
    }
    set_policy(before);
}
