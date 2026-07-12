// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_double_spend_proof_HPP
#define KTH_DOMAIN_MESSAGE_double_spend_proof_HPP

#include <istream>

#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/define.hpp>
// #include <kth/domain/message/block.hpp>
// #include <kth/domain/message/prefilled_transaction.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>


#include <kth/domain/concepts.hpp>

namespace kth::domain::message {

struct KD_API double_spend_proof {
    using ptr = std::shared_ptr<double_spend_proof>;
    using const_ptr = std::shared_ptr<double_spend_proof const>;
    using short_id = uint64_t;
    using short_id_list = std::vector<short_id>;

    struct spender {
        uint32_t version = 0;
        uint32_t out_sequence = 0;
        uint32_t locktime = 0;
        hash_digest prev_outs_hash = null_hash;
        hash_digest sequence_hash = null_hash;
        hash_digest outputs_hash = null_hash;
        data_chunk push_data;

        [[nodiscard]]
        bool is_valid() const {
            return version != 0 ||
                   out_sequence != 0 ||
                   locktime != 0 ||
                   prev_outs_hash != null_hash ||
                   sequence_hash != null_hash ||
                   outputs_hash != null_hash;
        }

        void reset() {
            version = 0;
            out_sequence = 0;
            locktime = 0;
            prev_outs_hash = null_hash;
            sequence_hash = null_hash;
            outputs_hash = null_hash;
            push_data.clear();
        }

        [[nodiscard]]
        friend bool operator==(spender const&, spender const&) = default;

        [[nodiscard]]
        size_t serialized_size() const {
            return sizeof(version) +
                sizeof(out_sequence) +
                sizeof(locktime) +
                hash_size +
                hash_size +
                hash_size +
                push_data.size();
        }

        [[nodiscard]]
        expect<void> to_data(byte_writer& writer) const {
            if (auto r = writer.write_little_endian<uint32_t>(version); ! r) return r;
            if (auto r = writer.write_little_endian<uint32_t>(out_sequence); ! r) return r;
            if (auto r = writer.write_little_endian<uint32_t>(locktime); ! r) return r;
            if (auto r = writer.write_hash(prev_outs_hash); ! r) return r;
            if (auto r = writer.write_hash(sequence_hash); ! r) return r;
            if (auto r = writer.write_hash(outputs_hash); ! r) return r;
            return writer.write_bytes(push_data);
        }

        static
        expect<spender> from_data(byte_reader& reader, uint32_t /*version*/);
    };

    double_spend_proof() = default;
    double_spend_proof(chain::output_point const& out_point, spender const& spender1, spender const& spender2);

    [[nodiscard]]
    friend bool operator==(double_spend_proof const&, double_spend_proof const&) = default;

    [[nodiscard]]
    chain::output_point const& out_point() const;
    void set_out_point(chain::output_point const& x);

    [[nodiscard]]
    spender const& spender1() const;
    void set_spender1(spender const& x);

    [[nodiscard]]
    spender const& spender2() const;
    void set_spender2(spender const& x);

    static
    expect<double_spend_proof> from_data(byte_reader& reader, uint32_t /*version*/);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t /*version*/) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t /*version*/) const {
        return out_point_.serialized_size() +
            spender1_.serialized_size() +
            spender2_.serialized_size();
    }

    hash_digest hash() const;

    static
    std::string const command;

    static
    uint32_t const version_minimum;

    static
    uint32_t const version_maximum;

private:
    chain::output_point out_point_;
    spender spender1_;
    spender spender2_;
};

hash_digest hash(double_spend_proof const& x);

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_double_spend_proof_HPP
