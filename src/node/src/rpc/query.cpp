// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/query.hpp>

#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include <kth/node/rpc/json.hpp>

namespace kth::node::rpc {

std::string transaction_to_hex(domain::chain::transaction const& tx) {
    return encode_base16(to_data_chunk(tx, /*wire*/ true));
}

std::string block_to_hex(domain::chain::block const& block) {
    return encode_base16(to_data_chunk(block));
}

std::string render_blockchain_info(
    std::string_view chain, std::size_t blocks, std::size_t headers,
    std::string_view best_block_hash, double difficulty) {

    writer w;
    w.begin_object();
    w.field("chain", chain);
    w.field("blocks", static_cast<std::uint64_t>(blocks));
    w.field("headers", static_cast<std::uint64_t>(headers));
    w.field("bestblockhash", best_block_hash);
    w.field("difficulty", difficulty);
    w.end_object();
    return w.str();
}

namespace {

// Compact nBits as an 8-char hex string (the getblockheader bits format).
std::string bits_to_hex(std::uint32_t bits) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(8) << bits;
    return os.str();
}

void write_hash_array(writer& w, hash_list const& hashes) {
    w.begin_array();
    for (auto const& h : hashes) {
        w.value(encode_hash(h));
    }
    w.end_array();
}

} // namespace

std::string render_hash_list(hash_list const& hashes) {
    writer w;
    write_hash_array(w, hashes);
    return w.str();
}

std::string render_mempool_info(blockchain::mempool_totals const& totals) {
    writer w;
    w.begin_object();
    w.field("size", static_cast<std::uint64_t>(totals.size));
    w.field("bytes", static_cast<std::uint64_t>(totals.bytes));
    w.field("total_fee", static_cast<std::uint64_t>(totals.total_fee));
    w.end_object();
    return w.str();
}

std::string render_mempool_entry(blockchain::mempool_entry_info const& entry,
                                 hash_list const& depends, hash_list const& spentby) {
    writer w;
    w.begin_object();
    w.field("fee", static_cast<std::uint64_t>(entry.fee));
    w.field("size", static_cast<std::uint64_t>(entry.size));
    w.field("time", static_cast<std::uint64_t>(entry.time));
    w.key("depends");
    write_hash_array(w, depends);
    w.key("spentby");
    write_hash_array(w, spentby);
    w.end_object();
    return w.str();
}

std::string header_to_hex(domain::chain::header const& header) {
    return encode_base16(to_data_chunk(header, /*wire*/ true));
}

std::string render_block_header(domain::chain::header const& header,
                                std::size_t height, hash_digest const& hash) {
    writer w;
    w.begin_object();
    w.field("hash", encode_hash(hash));
    w.field("height", static_cast<std::uint64_t>(height));
    w.field("version", static_cast<std::int64_t>(header.version()));
    w.field("previousblockhash", encode_hash(header.previous_block_hash()));
    w.field("merkleroot", encode_hash(header.merkle()));
    w.field("time", static_cast<std::int64_t>(header.timestamp()));
    w.field("bits", bits_to_hex(header.bits()));
    w.field("nonce", static_cast<std::int64_t>(header.nonce()));
    w.end_object();
    return w.str();
}

std::string render_validate_address(std::string_view address, domain::config::network net) {
    using domain::chain::script;
    using domain::wallet::payment_address;
    namespace cashaddr = domain::wallet::cashaddr;

    auto const invalid = [] {
        writer w;
        w.begin_object();
        w.field("isvalid", false);
        w.end_object();
        return w.str();
    };

    auto const parsed = payment_address::parse_from(address, net);
    if ( ! parsed) {
        return invalid();
    }

    // parse_from holds a cashaddr to this network's prefix, but takes a legacy
    // address of any network, so the version byte is checked here.
    auto const mainnet = net == domain::config::network::mainnet;
    auto const p2kh = mainnet ? payment_address::mainnet_p2kh : payment_address::testnet_p2kh;
    auto const p2sh = mainnet ? payment_address::mainnet_p2sh : payment_address::testnet_p2sh;
    if (parsed->version() != p2kh && parsed->version() != p2sh) {
        return invalid();
    }
    auto const is_script = parsed->version() == p2sh;

    // The output the address pays to. No standard output pays to a 32-byte
    // public key hash.
    auto const short_hash_address = parsed->hash_span().size() == short_hash_size;
    if ( ! short_hash_address && ! is_script) {
        return invalid();
    }
    script const pays_to{short_hash_address
        ? (is_script ? script::to_pay_script_hash_pattern(parsed->hash20())
                     : script::to_pay_public_key_hash_pattern(parsed->hash20()))
        : script::to_pay_script_hash_32_pattern(parsed->hash32())};

    // The cashaddr payload: the input's own, or for a legacy address that of
    // its plain cashaddr form. Its first 5-bit group is the address type, and
    // types 2 and 3 are the token-aware P2PKH and P2SH. Re-encoding it under
    // the network's prefix keeps the input's token signal.
    auto const prefix = std::string(payment_address::cashaddr_prefix_for(net));
    auto payload = cashaddr::decode(std::string(address), prefix).second;
    if (payload.empty()) {
        payload = cashaddr::decode(parsed->encoded_cashaddr(false), "").second;
    }
    if (payload.empty()) {
        return invalid();
    }

    writer w;
    w.begin_object();
    w.field("isvalid", true);
    w.field("address", cashaddr::encode(prefix, payload));
    w.field("scriptPubKey", encode_base16(pays_to.bytes()));
    w.field("isscript", is_script);
    w.field("istokenaware", (payload.front() & 0x02) != 0);
    w.end_object();
    return w.str();
}

} // namespace kth::node::rpc
