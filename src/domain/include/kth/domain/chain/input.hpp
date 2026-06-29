// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_CHAIN_INPUT_HPP
#define KTH_DOMAIN_CHAIN_INPUT_HPP

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <vector>

#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/wallet/payment_address.hpp>

#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/reader.hpp>
#include <kth/infrastructure/utility/writer.hpp>

#include <kth/domain/concepts.hpp>

namespace kth::domain::chain {

struct KD_API input {
    using list = std::vector<input>;

    // Constructors.
    //-------------------------------------------------------------------------

    input() = default;
    input(output_point const& previous_output, chain::script const& script, uint32_t sequence);
    input(output_point&& previous_output, chain::script&& script, uint32_t sequence);

    // Operators.
    //-------------------------------------------------------------------------

    friend
    bool operator==(input const&, input const&);

    friend
    bool operator!=(input const&, input const&);

    // Deserialization.
    //-------------------------------------------------------------------------

    static
    expect<input> from_data(byte_reader& reader, bool wire);

    [[nodiscard]]
    bool is_valid() const;

    // Serialization.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    data_chunk to_data(bool wire = true) const;

    void to_data(data_sink& stream, bool wire = true) const;

    template <typename W>
    void to_data(W& sink, bool wire = true) const {
        previous_output_.to_data(sink, wire);
        script_.to_data(sink, true);
        sink.write_4_bytes_little_endian(sequence_);
    }

    // Properties (size, accessors).
    //-------------------------------------------------------------------------

    /// This accounts for wire, but does not read or write it.
    [[nodiscard]]
    size_t serialized_size(bool wire = true) const;

    output_point& previous_output();

    [[nodiscard]]
    output_point const& previous_output() const;

    void set_previous_output(output_point const& value);
    void set_previous_output(output_point&& value);

    // [[deprecated]] // unsafe
    chain::script& script();

    [[nodiscard]]
    chain::script const& script() const;

    void set_script(chain::script const& value);
    void set_script(chain::script&& value);

    [[nodiscard]]
    uint32_t sequence() const;

    void set_sequence(uint32_t value);

    /// The first payment address extracted (may be invalid).
    /// NOTE: not cached — recomputed on every call.
    [[nodiscard]]
    wallet::payment_address address() const;

    /// The payment addresses extracted from this input as a standard script.
    /// NOTE: not cached — caller owns any caching it needs.
    [[nodiscard]]
    wallet::payment_address::list addresses() const;

    // Validation.
    //-------------------------------------------------------------------------

    [[nodiscard]]
    bool is_final() const;

    [[nodiscard]]
    bool is_locked(size_t block_height, uint32_t median_time_past) const;

    [[nodiscard]]
    size_t signature_operations(bool bip16, bool bip141) const;

    [[nodiscard]]
    expect<chain::script> extract_embedded_script() const;

    void reset();

private:
    output_point previous_output_;
    chain::script script_;
    uint32_t sequence_{0};
};

} // namespace kth::domain::chain

#endif // KTH_DOMAIN_CHAIN_INPUT_HPP
