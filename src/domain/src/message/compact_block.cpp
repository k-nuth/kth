// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/compact_block.hpp>

#include <initializer_list>

// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/endian.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::domain::message {

namespace {

// Use the wire minimum here so historical short transactions still decode.
constexpr size_t min_wire_transaction_size = 10;
constexpr size_t short_id_wire_size = 6;
constexpr size_t min_prefilled_wire_size = 1 + min_wire_transaction_size;

} // namespace

std::string const compact_block::command = "cmpctblock";
uint32_t const compact_block::version_minimum = version::level::bip152;
uint32_t const compact_block::version_maximum = version::level::bip152;

uint64_t compact_short_id(hash_digest const& header_nonce_hash, hash_digest const& txid) {
    auto const k0 = from_little_endian_unsafe<uint64_t>(header_nonce_hash);
    auto const k1 = from_little_endian_unsafe<uint64_t>(
        std::span{header_nonce_hash}.subspan(sizeof(uint64_t)));
    return sip_hash_uint256(k0, k1, txid) & uint64_t{0x0000ffffffffffff};
}

// static
compact_block compact_block::factory_from_block(message::block const& block) {
    // BIP152: the nonce salts the SipHash short-id computation, so it must be
    // unpredictable to an adversary (otherwise short-id collisions can be
    // ground out to force block-reconstruction failures). Use the CSPRNG.
    auto const nonce = pseudo_random::generate<uint64_t>();

    // Index 0 (the coinbase) is always in range, so `create` cannot fail here.
    prefilled_transaction::list prefilled_list{
        prefilled_transaction::create(0, block.transactions()[0]).value()};

    auto const header_hash = hash(block, nonce);

    compact_block::short_id_list short_ids_list;
    short_ids_list.reserve(block.transactions().size() - 1);
    for (size_t i = 1; i < block.transactions().size(); ++i) {
        short_ids_list.push_back(compact_short_id(header_hash, block.transactions()[i].hash()));
    }

    // A block always carries a valid header and a coinbase, so the result is
    // never the empty sentinel.
    return compact_block{block.header(), nonce, std::move(short_ids_list), std::move(prefilled_list)};
}

// static


compact_block::compact_block(chain::header header, uint64_t nonce, short_id_list short_ids, prefilled_transaction::list transactions)
    : header_(header)
    , nonce_(nonce)
    , short_ids_(std::move(short_ids))
    , transactions_(std::move(transactions))
{}

// compact_block::compact_block(compact_block&& x) noexcept
//     // : compact_block(x.header_, x.nonce_, std::move(x.short_ids_), std::move(x.transactions_))
//     : header_(x.header_)
//     , nonce_(x.nonce_)
//     , short_ids_(std::move(x.short_ids_))
//     , transactions_(std::move(x.transactions_))
// {}


// compact_block& compact_block::operator=(compact_block&& x) noexcept {
//     header_ = x.header_;
//     nonce_ = x.nonce_;
//     short_ids_ = std::move(x.short_ids_);
//     transactions_ = std::move(x.transactions_);
//     return *this;
// }

// Deserialization.
//-----------------------------------------------------------------------------

// static
expect<compact_block> compact_block::from_data(byte_reader& reader, uint32_t version) {
    auto const header = chain::header::from_data(reader);
    if ( ! header) {
        return std::unexpected(header.error());
    }

    auto const nonce = reader.read_little_endian<uint64_t>();
    if ( ! nonce) {
        return std::unexpected(nonce.error());
    }

    auto const short_id_count = reader.read_size_little_endian();
    if ( ! short_id_count) {
        return std::unexpected(short_id_count.error());
    }
    // Refuse impossible counts before reserving memory.
    if (*short_id_count > max_compact_transactions()
        || (*short_id_count > 0 && reader.remaining_size() / short_id_wire_size < *short_id_count)) {
        return std::unexpected(error::invalid_compact_block);
    }

    short_id_list short_ids;
    short_ids.reserve(*short_id_count);
    for (size_t i = 0; i < *short_id_count; ++i) {
        auto const lsb = reader.read_little_endian<uint32_t>();
        if ( ! lsb) {
            return std::unexpected(lsb.error());
        }
        auto const msb = reader.read_little_endian<uint16_t>();
        if ( ! msb) {
            return std::unexpected(msb.error());
        }
        short_ids.emplace_back((uint64_t(*msb) << 32) | uint64_t(*lsb));
        //short_ids_.push_back(source.read_mini_hash());
    }

    auto const prefilled_count = reader.read_size_little_endian();
    if ( ! prefilled_count) {
        return std::unexpected(prefilled_count.error());
    }
    if (*prefilled_count > max_compact_transactions() - *short_id_count
        || (*prefilled_count > 0 && reader.remaining_size() / min_prefilled_wire_size < *prefilled_count)) {
        return std::unexpected(error::invalid_compact_block);
    }

    // Keep absolute indexes in memory; only the wire form is differential.
    auto const total = *short_id_count + *prefilled_count;
    prefilled_transaction::list txs;
    txs.reserve(*prefilled_count);
    uint64_t next = 0;
    for (size_t i = 0; i < *prefilled_count; ++i) {
        auto const differential = reader.read_variable_little_endian();
        if ( ! differential) {
            return std::unexpected(differential.error());
        }
        if (*differential >= total - next) {
            return std::unexpected(error::invalid_compact_block);
        }
        auto const absolute = next + *differential;
        next = absolute + 1;
        auto transaction = chain::transaction::from_data(reader, true);
        if ( ! transaction) {
            return std::unexpected(transaction.error());
        }
        // The transaction-count bound is smaller than the prefilled index cap.
        txs.push_back(prefilled_transaction::create(absolute, std::move(*transaction)).value());
    }

    if (version < compact_block::version_minimum) {
        return std::unexpected(error::version_too_low);
    }

    return compact_block(*header, *nonce, std::move(short_ids), std::move(txs));
}

// Serialization.
//-----------------------------------------------------------------------------



size_t compact_block::serialized_size(uint32_t /*version*/) const {
    //std::println("compact_block::serialized_size");

    auto size = chain::header::satoshi_fixed_size() +
                infrastructure::message::variable_uint_size(short_ids_.size()) +
                (short_ids_.size() * 6u) +
                infrastructure::message::variable_uint_size(transactions_.size()) + 8u;

    // Use the differential width; retain an upper bound for invalid lists.
    uint64_t previous = 0;
    bool started = false;
    for (auto const& tx : transactions_) {
        uint64_t differential = tx.index();
        if ( ! started) {
            previous = tx.index();
            started = true;
        } else if (tx.index() > previous) {
            differential = tx.index() - previous - 1;
            previous = tx.index();
        }
        size += infrastructure::message::variable_uint_size(differential)
            + tx.transaction().serialized_size(true);
    }

    return size;
}

chain::header const& compact_block::header() const {
    return header_;
}

uint64_t compact_block::nonce() const {
    return nonce_;
}

compact_block::short_id_list const& compact_block::short_ids() const {
    return short_ids_;
}

prefilled_transaction::list const& compact_block::transactions() const {
    return transactions_;
}

hash_digest hash(compact_block const& block) {
    auto const size = chain::header::satoshi_fixed_size() + sizeof(block.nonce());
    data_chunk buf(size);
    byte_writer writer(buf);
    auto const r1 = block.header().to_data(writer, true);
    auto const r2 = writer.write_little_endian<uint64_t>(block.nonce());
    KTH_ASSERT(r1.has_value() && r2.has_value());
    return sha256_hash(buf);
}

expect<void> compact_block::to_data(byte_writer& writer, uint32_t /*version*/) const noexcept {
        if (auto r = header_.to_data(writer, true); ! r) return r;
        if (auto r = writer.write_little_endian<uint64_t>(nonce_); ! r) return r;
        if (auto r = writer.write_variable_little_endian(short_ids_.size()); ! r) return r;

        for (auto const& element : short_ids_) {
            //sink.write_mini_hash(element);
            uint32_t lsb = element & 0xffffffff;
            uint16_t msb = (element >> 32) & 0xffff;
            if (auto r = writer.write_little_endian<uint32_t>(lsb); ! r) return r;
            if (auto r = writer.write_little_endian<uint16_t>(msb); ! r) return r;
        }

        if (auto r = writer.write_variable_little_endian(transactions_.size()); ! r) return r;

        // Differentially encoded indexes. prefilled_transaction::to_data writes
        // the absolute index, which is only right for a list of one.
        uint64_t previous = 0;
        bool started = false;
        for (auto const& element : transactions_) {
            if (started && element.index() <= previous) {
                return std::unexpected(error::invalid_compact_block);
            }
            auto const differential = started ? element.index() - previous - 1 : element.index();
            if (auto r = writer.write_variable_little_endian(differential); ! r) return r;
            if (auto r = element.transaction().to_data(writer, true); ! r) return r;
            previous = element.index();
            started = true;
        }
        return {};
}

} // namespace kth::domain::message
