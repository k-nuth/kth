// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MESSAGE_INVENTORY_VECTOR_HPP_
#define KTH_DOMAIN_MESSAGE_INVENTORY_VECTOR_HPP_

#include <cstdint>
#include <istream>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/domain/concepts.hpp>
#include <kth/domain/deserialization.hpp>

namespace kth::domain::message {

struct KD_API inventory_vector {
    using list = std::vector<inventory_vector>;

    enum class type_id : uint32_t {
        error = 0,
        transaction = 1,
        block = 2,
        filtered_block = 3,
        compact_block = 4,
        double_spend_proof = 0x94a0,
    };

    static
    type_id to_type(uint32_t value);

    static
    uint32_t to_number(type_id type);

    static
    std::string to_string(type_id type);

    static
    size_t satoshi_fixed_size(uint32_t version);

    inventory_vector();
    inventory_vector(type_id type, hash_digest const& hash);
    inventory_vector(inventory_vector const& x) = default;
    inventory_vector(inventory_vector&& x) = default;

    // This class is move assignable but not copy assignable.
    inventory_vector& operator=(inventory_vector&& x) = default;
    inventory_vector& operator=(inventory_vector const& x) = default;

    [[nodiscard]]
    friend bool operator==(inventory_vector const&, inventory_vector const&) = default;


    [[nodiscard]]
    type_id type() const;

    void set_type(type_id value);

    hash_digest& hash();

    [[nodiscard]]
    hash_digest const& hash() const;

    void set_hash(hash_digest const& value);

    [[nodiscard]]
    bool is_block_type() const;

    [[nodiscard]]
    bool is_transaction_type() const;

    [[nodiscard]]
    bool is_double_spend_proof_type() const;

    static
    expect<inventory_vector> from_data(byte_reader& reader, uint32_t version);

    [[nodiscard]]
    expect<void> to_data(byte_writer& writer, uint32_t version) const;

    [[nodiscard]]
    size_t serialized_size(uint32_t version) const;

private:
    type_id type_{type_id::error};
    hash_digest hash_{null_hash};
};

} // namespace kth::domain::message

#endif // KTH_DOMAIN_MESSAGE_INVENTORY_VECTOR_HPP_
