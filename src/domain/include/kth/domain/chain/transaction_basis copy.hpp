// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_TRANSACTION_BASIS_HPP
#define KTH_DOMAIN_CHAIN_TRANSACTION_BASIS_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/rule_fork.hpp>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/writer.hpp>


#include <kth/domain/concepts.hpp>


#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/std.h>

#ifdef JEMALLOC
#include <jemalloc/jemalloc.h>

inline
void print_memory_usage(std::string_view message) {
    size_t epoch = 1;
    size_t sz = sizeof(size_t);
    size_t allocated = 0;
    size_t active = 0;
    size_t resident = 0;
    size_t retained = 0;

    mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
    mallctl("epoch", NULL, NULL, &epoch, sizeof(epoch));
    mallctl("stats.allocated", &allocated, &sz, nullptr, 0);
    mallctl("stats.active", &active, &sz, nullptr, 0);
    mallctl("stats.resident", &resident, &sz, nullptr, 0);
    mallctl("stats.retained", &retained, &sz, nullptr, 0);

    double allocated_mb = allocated / 1'000'000.0;
    double active_mb = active / 1'000'000.0;
    double resident_mb = resident / 1'000'000.0;
    double retained_mb = retained / 1'000'000.0;

    fmt::print("{:<60} - allocated: {:>10} - active: {:>10} - resident: {:>10} - retained: {:>10}\n", message, allocated_mb, active_mb, resident_mb, retained_mb);
}
#else
inline
void print_memory_usage(std::string_view message) {
    fmt::print("{} - memory usage not available\n", message);
}
#endif


#ifndef JEMALLOC
#error "JEMALLOC must be defined"
#endif


namespace kth::domain::chain {

namespace detail {
// Read a length-prefixed collection of inputs or outputs from the source.
template <class Source, class Put>
bool read(Source& source, std::vector<Put>& puts, bool wire, bool witness) {
    auto result = true;
    auto const count = source.read_size_little_endian();
    fmt::print("count: {}\n", count);

    // Guard against potential for arbitary memory allocation.
    if (count > static_absolute_max_block_size()) {
        fmt::print("count > static_absolute_max_block_size -> invalidate\n");
        source.invalidate();
    } else {
        fmt::print("count <= static_absolute_max_block_size -> resize\n");
        puts.resize(count);
    }

    auto const deserialize = [&](Put& put) {
        std::string message = fmt::format("before parsing element");
        print_memory_usage(message);

        result = result && put.from_data(source, wire);

        message = fmt::format("after parsing element");
        print_memory_usage(message);

        fmt::print("result: {}\n", result);

#ifndef NDEBUG
        fmt::print("running put.script().operations()\n");
        put.script().operations();
#endif
    };

    std::for_each(puts.begin(), puts.end(), deserialize);
    fmt::print("after std::for_each\n");
    print_memory_usage("after std::for_each");

    return result;
}

// Write a length-prefixed collection of inputs or outputs to the sink.
template <class Sink, class Put>
void write(Sink& sink, const std::vector<Put>& puts, bool wire, bool witness) {
    sink.write_variable_little_endian(puts.size());

    auto const serialize = [&](const Put& put) {
        put.to_data(sink, wire);
    };

    std::for_each(puts.begin(), puts.end(), serialize);
}

} // namespace detail


class transaction_basis;

hash_digest hash(transaction_basis const& tx, bool witness);
hash_digest outputs_hash(transaction_basis const& tx);
hash_digest inpoints_hash(transaction_basis const& tx);
hash_digest sequences_hash(transaction_basis const& tx);

hash_digest to_outputs(transaction_basis const& tx);
hash_digest to_inpoints(transaction_basis const& tx);
hash_digest to_sequences(transaction_basis const& tx);

uint64_t total_input_value(transaction_basis const& tx);
uint64_t total_output_value(transaction_basis const& tx);
uint64_t fees(transaction_basis const& tx);
bool is_overspent(transaction_basis const& tx);

struct KD_API transaction_basis {
    using ins = input::list;
    using outs = output::list;
    using list = std::vector<transaction_basis>;
    using hash_ptr = std::shared_ptr<hash_digest>;

    // Constructors.
    //-----------------------------------------------------------------------------

    transaction_basis() = default;
    transaction_basis(uint32_t version, uint32_t locktime, ins const& inputs, outs const& outputs);
    transaction_basis(uint32_t version, uint32_t locktime, ins&& inputs, outs&& outputs);

    // transaction_basis(transaction_basis const& x) = default;
    // transaction_basis(transaction_basis&& x) = default;
    // transaction_basis& operator=(transaction_basis const& x) = default;
    // transaction_basis& operator=(transaction_basis&& x) = default;


    // Operators.
    //-----------------------------------------------------------------------------

    bool operator==(transaction_basis const& x) const;
    bool operator!=(transaction_basis const& x) const;

    // Deserialization.
    //-----------------------------------------------------------------------------

    // Witness is not used by outputs, just for template normalization.
    template <typename R, KTH_IS_READER(R)>
    bool from_data(R& source, bool wire = true, bool witness = false) {
        reset();
        fmt::print("transaction_basis::from_data\n");
        fmt::print("wire: {}\n", wire);
        fmt::print("witness: {}\n", witness);

        if (wire) {
            // Wire (satoshi protocol) deserialization.
            version_ = source.read_4_bytes_little_endian();
            fmt::print("version_: {}\n", version_);

            std::string message = fmt::format("before parsing inputs");
            print_memory_usage(message);

            detail::read(source, inputs_, wire);

            message = fmt::format("after parsing inputs");
            print_memory_usage(message);

            auto const marker = false;
            fmt::print("marker: {}\n", marker);
#endif

            // This is always enabled so caller should validate with is_segregated.
            if (marker) {
                fmt::print("marker is true\n");
                // Skip over the peeked witness flag.
                source.skip(1);
                detail::read(source, inputs_, wire);
                detail::read(source, outputs_, wire);
            } else {
                fmt::print("marker is false\n");
                message = fmt::format("before parsing outputs");
                print_memory_usage(message);
                detail::read(source, outputs_, wire);
                message = fmt::format("after parsing outputs");
                print_memory_usage(message);
            }

            locktime_ = source.read_4_bytes_little_endian();
            fmt::print("locktime_: {}\n", locktime_);
        } else {
            // Database (outputs forward) serialization.
            // Witness data is managed internal to inputs.
            detail::read(source, outputs_, wire);
            detail::read(source, inputs_, wire);
            auto const locktime = source.read_variable_little_endian();
            auto const version = source.read_variable_little_endian();

            if (locktime > max_uint32 || version > max_uint32) {
                source.invalidate();
            }

            locktime_ = static_cast<uint32_t>(locktime);
            version_ = static_cast<uint32_t>(version);
        }

        if ( ! source) {
            fmt::print("source is invalid\n");
            reset();
        } else {
            fmt::print("source is valid\n");
        }

        return source;
    }

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true, bool witness = false) const;

    void to_data(data_sink& stream, bool wire = true, bool witness = false) const;

    // Witness is not used by outputs, just for template normalization.
    template <typename W>
    void to_data(W& sink, bool wire = true, bool witness = false) const {
        if (wire) {
            // Wire (satoshi protocol) serialization.
            sink.write_4_bytes_little_endian(version_);

            detail::write(sink, inputs_, wire);
            detail::write(sink, outputs_, wire);

            sink.write_4_bytes_little_endian(locktime_);
        } else {
            // Database (outputs forward) serialization.
            // Witness data is managed internal to inputs.
            detail::write(sink, outputs_, wire);
            detail::write(sink, inputs_, wire);
            sink.write_variable_little_endian(locktime_);
            sink.write_variable_little_endian(version_);
        }
    }

    // Properties (size, accessors, cache).
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    size_t serialized_size(bool wire = true, bool witness = false) const;

    [[nodiscard]]
    uint32_t version() const;

    void set_version(uint32_t value);

    [[nodiscard]]
    uint32_t locktime() const;

    void set_locktime(uint32_t value);

    // [[deprecated]] // unsafe
    ins& inputs();

    [[nodiscard]]
    const ins& inputs() const;

    void set_inputs(const ins& value);
    void set_inputs(ins&& value);

    // [[deprecated]] // unsafe
    outs& outputs();

    [[nodiscard]]
    const outs& outputs() const;

    void set_outputs(const outs& value);
    void set_outputs(outs&& value);

    // Validation.
    //-----------------------------------------------------------------------------

    [[nodiscard]]
    uint64_t fees() const;

    [[nodiscard]]
    point::list previous_outputs() const;

    [[nodiscard]]
    point::list missing_previous_outputs() const;

    [[nodiscard]]
    hash_list missing_previous_transactions() const;

    [[nodiscard]]
    uint64_t total_input_value() const;

    [[nodiscard]]
    size_t signature_operations() const;

    [[nodiscard]]
    size_t signature_operations(bool bip16, bool bip141) const;

    [[nodiscard]]
    bool is_coinbase() const;

    [[nodiscard]]
    bool is_null_non_coinbase() const;

    [[nodiscard]]
    bool is_oversized_coinbase() const;

    [[nodiscard]]
    bool is_mature(size_t height) const;

    [[nodiscard]]
    bool is_internal_double_spend() const;

    [[nodiscard]]
    bool is_double_spend(bool include_unconfirmed) const;

    [[nodiscard]]
    bool is_dusty(uint64_t minimum_output_value) const;

    [[nodiscard]]
    bool is_missing_previous_outputs() const;

    [[nodiscard]]
    bool is_final(size_t block_height, uint32_t block_time) const;

    [[nodiscard]]
    bool is_locked(size_t block_height, uint32_t median_time_past) const;

    [[nodiscard]]
    bool is_locktime_conflict() const;

    [[nodiscard]]
    code check(uint64_t total_output_value, size_t max_block_size, bool transaction_pool = true, bool retarget = true) const;

    [[nodiscard]]
    size_t min_tx_size(chain_state const& state) const;

    [[nodiscard]]
    code accept(chain_state const& state, bool is_segregated, bool is_overspent, bool is_duplicated, bool transaction_pool = true) const;

    [[nodiscard]]
    bool is_standard() const;

// protected:
    void reset();

protected:
    [[nodiscard]]
    bool all_inputs_final() const;

private:
    uint32_t version_{0};
    uint32_t locktime_{0};
    input::list inputs_;
    output::list outputs_;
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_TRANSACTION_BASIS_HPP
