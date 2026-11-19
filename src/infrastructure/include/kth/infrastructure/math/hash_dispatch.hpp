// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_HASH_DISPATCH_HPP
#define KTH_INFRASTRUCTURE_HASH_DISPATCH_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Where a batch of messages is hashed.
//
// A machine has more than one way to hash a batch: the processor, several
// messages in step, and a GPU when there is one. Which is faster depends on the
// batch and on the machine, so it is decided here, for every batch, from what
// hashing has been measured to cost on this machine: first by a short
// calibration, then by the time each real batch took.
//
// The decision is not all or nothing. The device hashes one message per thread,
// block after block, so a launch lasts as long as its longest message; a batch
// is therefore split by length, the short messages to the device and the long
// ones to the processor, at whatever length makes the whole the fastest.

namespace kth::hash_dispatch {

// What hashing costs on this machine, in nanoseconds.
//
// On the processor a batch costs a fixed amount per message plus an amount per
// byte. On the device it costs a fixed amount per launch, an amount per byte
// (gathering them and moving them to the device), and an amount per 64-byte
// block of the longest message of the launch, which is the time one thread of
// the device takes for a block.
struct cost_model {
    double cpu_per_message{0};
    double cpu_per_byte{0};

    // Whether there is a device to send work to. The three amounts below mean
    // something only then.
    bool gpu{false};
    double gpu_per_launch{0};
    double gpu_per_byte{0};
    double gpu_per_longest_block{0};

    // How many real batches have corrected each side since the calibration.
    uint64_t cpu_batches{0};
    uint64_t gpu_batches{0};

    [[nodiscard]]
    friend bool operator==(cost_model const&, cost_model const&) = default;

    // The time the model gives for hashing `messages` messages of `bytes`
    // bytes in all on the processor.
    [[nodiscard]]
    double cpu_time(uint64_t messages, uint64_t bytes) const {
        return cpu_per_message * double(messages) + cpu_per_byte * double(bytes);
    }

    // The time it gives for one launch of `bytes` bytes in all whose longest
    // message is `longest` bytes.
    [[nodiscard]]
    double gpu_time(uint64_t bytes, uint64_t longest) const;

    // One line of text holding every amount, and back. `from_text` gives
    // nothing for a line it does not recognise, so a file from another
    // version, or a damaged one, is ignored and the machine is measured again.
    [[nodiscard]] KI_API std::string to_text() const;
    [[nodiscard]] KI_API static std::optional<cost_model> from_text(std::string_view text);
};

// How one batch is split.
struct plan {
    // Messages shorter than this many bytes go to the device; the rest, and
    // every message when this is zero, are hashed on the processor.
    uint64_t gpu_below{0};
    size_t gpu_messages{0};
    uint64_t gpu_bytes{0};
    uint64_t gpu_longest{0};

    // The model's time for the whole batch on the processor, and for the batch
    // split this way.
    double all_cpu_ns{0};
    double planned_ns{0};
};

// The fewest messages a launch is considered for.
inline constexpr size_t minimum_gpu_messages = 256;

// The split of `messages` that `model` gives the least time for. With no
// device in the model, or fewer messages than a launch is considered for, the
// whole batch stays on the processor.
[[nodiscard]] KI_API
plan choose(cost_model const& model, std::span<byte_span const> messages);

// A model corrected by what a real batch took: the amounts of one side scaled
// a part of the way towards what would have predicted `measured_ns`. A single
// batch moves them little, so one odd measurement does not undo the rest; a
// machine that has changed (another program on the device, a slower disk
// behind the bytes) is followed within a few dozen batches.
[[nodiscard]] KI_API
cost_model observe_cpu(cost_model model, uint64_t messages, uint64_t bytes, double measured_ns);
[[nodiscard]] KI_API
cost_model observe_gpu(cost_model model, uint64_t bytes, uint64_t longest, double measured_ns);

// Whether the device may be used at all. `automatic` decides per batch.
enum class policy {
    automatic,
    cpu_only,
};

// What KTH_HASH_DISPATCH asks for: "cpu" keeps every batch on the processor.
[[nodiscard]] KI_API policy policy_from_environment();

// The model of one task that hashes batches, and the decision made with it.
//
// It belongs to that task: nothing in it is shared, so two tasks that hash
// each keep their own, and each learns from the batches it hashed. A node that
// kept the model of its last run puts it back with set_model before hashing,
// and reads `model` when it stops.
struct dispatcher {
    cost_model model{};
    // Whether `model` holds a measurement of this machine. Until it does, the
    // first batch large enough for a launch calibrates.
    bool measured{false};
    policy how{policy_from_environment()};

    // out[i] = bitcoin_hash(messages[i]), each part of the batch hashed where
    // the model says it is fastest, and the model corrected by what it took.
    KI_API void hash(std::span<byte_span const> messages, std::span<hash_digest> out);

    // Take a model measured before, on this machine or in another run.
    KI_API void set_model(cost_model const& value);

    // Measure this machine now, replacing the model. Takes a fraction of a
    // second.
    KI_API void calibrate();
};

} // namespace kth::hash_dispatch

#endif // KTH_INFRASTRUCTURE_HASH_DISPATCH_HPP
