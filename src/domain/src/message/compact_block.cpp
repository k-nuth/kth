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
#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>
#include <kth/infrastructure/utility/limits.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>
#include <kth/infrastructure/utility/pseudo_random_broken_do_not_use.hpp>

namespace kth::domain::message {

std::string const compact_block::command = "cmpctblock";
uint32_t const compact_block::version_minimum = version::level::bip152;
uint32_t const compact_block::version_maximum = version::level::bip152;

compact_block compact_block::factory_from_block(message::block const& blk) {
    compact_block instance;
    instance.from_block(blk);
    return instance;
}

compact_block::compact_block(chain::header const& header, uint64_t nonce, short_id_list const& short_ids, prefilled_transaction::list const& transactions)
    : header_(header)
    , nonce_(nonce)
    , short_ids_(short_ids)
    , transactions_(transactions)
{}

compact_block::compact_block(chain::header const& header, uint64_t nonce, short_id_list&& short_ids, prefilled_transaction::list&& transactions)
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

bool compact_block::operator==(compact_block const& x) const {
    return (header_ == x.header_) && (nonce_ == x.nonce_) && (short_ids_ == x.short_ids_) && (transactions_ == x.transactions_);
}

bool compact_block::operator!=(compact_block const& x) const {
    return !(*this == x);
}

bool compact_block::is_valid() const {
    //std::println("compact_block::is_valid");

    return header_.is_valid()
        && ! short_ids_.empty()
        && ! transactions_.empty();
}

void compact_block::reset() {
    //std::println("compact_block::reset");

    header_ = chain::header{};
    nonce_ = 0;
    short_ids_.clear();
    short_ids_.shrink_to_fit();
    transactions_.clear();
    transactions_.shrink_to_fit();
}

bool compact_block::from_block(message::block const& block) {
    reset();

    header_ = block.header();
    nonce_ = pseudo_random_broken_do_not_use(1, max_uint64);

    prefilled_transaction::list prefilled_list{prefilled_transaction{0, block.transactions()[0]}};

    auto header_hash = hash(block, nonce_);

    auto k0 = from_little_endian_unsafe<uint64_t>(header_hash.begin());
    auto k1 = from_little_endian_unsafe<uint64_t>(header_hash.begin() + sizeof(uint64_t));

    compact_block::short_id_list short_ids_list;
    short_ids_list.reserve(block.transactions().size() - 1);
    for (size_t i = 1; i < block.transactions().size(); ++i) {
        uint64_t shortid = sip_hash_uint256(
            k0,
            k1,
            block.transactions()[i].hash()
        ) & uint64_t(0xffffffffffff);
        short_ids_list.push_back(shortid);
    }

    short_ids_ = std::move(short_ids_list);
    transactions_ = std::move(prefilled_list);

    return true;
}

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

data_chunk compact_block::to_data(uint32_t version) const {
    data_chunk data;
    auto const size = serialized_size(version);
    data.reserve(size);
    data_sink ostream(data);
    to_data(version, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

void compact_block::to_data(uint32_t version, data_sink& stream) const {
    //std::println("compact_block::to_data 2");

    ostream_writer sink_w(stream);
    to_data(version, sink_w);
}

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

chain::header& compact_block::header() {
    //std::println("compact_block::header");

    return header_;
}

chain::header const& compact_block::header() const {
    //std::println("compact_block::header 2");

    return header_;
}

void compact_block::set_header(chain::header const& value) {
    //std::println("compact_block::set_header");

    header_ = value;
}

uint64_t compact_block::nonce() const {
    //std::println("compact_block::nonce");

    return nonce_;
}

void compact_block::set_nonce(uint64_t value) {
    //std::println("compact_block::set_nonce");
    nonce_ = value;
}

compact_block::short_id_list& compact_block::short_ids() {
    //std::println("compact_block::short_ids");
    return short_ids_;
}

compact_block::short_id_list const& compact_block::short_ids() const {
    //std::println("compact_block::short_ids 2");
    return short_ids_;
}

void compact_block::set_short_ids(short_id_list const& value) {
    short_ids_ = value;
}

void compact_block::set_short_ids(short_id_list&& value) {
    short_ids_ = std::move(value);
}

prefilled_transaction::list& compact_block::transactions() {
    return transactions_;
}

prefilled_transaction::list const& compact_block::transactions() const {
    return transactions_;
}

void compact_block::set_transactions(prefilled_transaction::list const& value) {
    transactions_ = value;
}

void compact_block::set_transactions(prefilled_transaction::list&& value) {
    transactions_ = std::move(value);
}

// void to_data_header_nonce(compact_block const& block, std::ostream& stream) {
void to_data_header_nonce(compact_block const& block, data_sink& stream) {
    ostream_writer sink_w(stream);
    to_data_header_nonce(block, sink_w);
}

data_chunk to_data_header_nonce(compact_block const& block) {
    //std::println("compact_block::to_data");

    data_chunk data;
    auto size = chain::header::satoshi_fixed_size() + sizeof(block.nonce());

    data.reserve(size);
    data_sink ostream(data);
    to_data_header_nonce(block, ostream);
    ostream.flush();
    KTH_ASSERT(data.size() == size);
    return data;
}

hash_digest hash(compact_block const& block) {
    return sha256_hash(to_data_header_nonce(block));
}

} // namespace kth::domain::message
