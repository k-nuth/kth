// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/message/compact_block.hpp>

#include <initializer_list>

// #include <kth/infrastructure/message/message_tools.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/math/sip_hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::domain::message {

std::string const compact_block::command = "cmpctblock";
uint32_t const compact_block::version_minimum = version::level::bip152;
uint32_t const compact_block::version_maximum = version::level::bip152;

// static
compact_block compact_block::factory_from_block(message::block const& block) {
    // BIP152: the nonce salts the SipHash short-id computation, so it must be
    // unpredictable to an adversary (otherwise short-id collisions can be
    // ground out to force block-reconstruction failures). Use the CSPRNG.
    auto const nonce = pseudo_random::generate<uint64_t>();

    // Index 0 (the coinbase) is always in range, so `create` cannot fail here.
    prefilled_transaction::list prefilled_list{
        prefilled_transaction::create(0, block.transactions()[0]).value()};

    auto header_hash = hash(block, nonce);
    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash);
    auto k1 = from_little_endian_unsafe<uint64_t>(std::span{header_hash}.subspan(sizeof(uint64_t)));

    compact_block::short_id_list short_ids_list;
    short_ids_list.reserve(block.transactions().size() - 1);
    for (size_t i = 1; i < block.transactions().size(); ++i) {
        uint64_t shortid = sip_hash_uint256(k0, k1, block.transactions()[i].hash()) & uint64_t(0xffffffffffff);
        short_ids_list.push_back(shortid);
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
    if (*short_id_count > static_absolute_max_block_size()) {
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

    auto txs = read_collection<prefilled_transaction>(reader, version);
    if ( ! txs) {
        return std::unexpected(txs.error());
    }

    if (version < compact_block::version_minimum) {
        return std::unexpected(error::version_too_low);
    }

    return compact_block(*header, *nonce, std::move(short_ids), std::move(*txs));
}

// Serialization.
//-----------------------------------------------------------------------------



size_t compact_block::serialized_size(uint32_t version) const {
    //std::println("compact_block::serialized_size");

    auto size = chain::header::satoshi_fixed_size() +
                infrastructure::message::variable_uint_size(short_ids_.size()) +
                (short_ids_.size() * 6u) +
                infrastructure::message::variable_uint_size(transactions_.size()) + 8u;

    // NOTE: Witness flag is controlled by prefilled tx
    for (auto const& tx : transactions_) {
        size += tx.serialized_size(version);
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

expect<void> compact_block::to_data(byte_writer& writer, uint32_t version) const {
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

        // NOTE: Witness flag is controlled by prefilled tx
        for (auto const& element : transactions_) {
            if (auto r = element.to_data(writer, version); ! r) return r;
        }
        return {};
}

} // namespace kth::domain::message
