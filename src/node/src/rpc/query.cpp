// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/rpc/query.hpp>

#include <algorithm>
#include <charconv>
#include <system_error>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <utility>

#include <fmt/format.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/message/version.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/constants/common.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/formats/bitcoin.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/byte_writer.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/units.hpp>

#include <kth/node/rpc/json.hpp>

namespace kth::node::rpc {

std::string transaction_to_hex(domain::chain::transaction const& tx) {
    return encode_base16(to_data_chunk(tx, /*wire*/ true));
}

std::string block_to_hex(domain::chain::block const& block) {
    return encode_base16(to_data_chunk(block));
}

std::string render_blockchain_info(blockchain_info const& info) {
    // By height: kth keeps no per-block transaction counts to weigh it by.
    auto const progress = info.headers == 0 ? 1.0
        : std::min(1.0, double(info.blocks) / double(info.headers));

    writer w;
    w.begin_object();
    w.field("chain", info.chain);
    w.field("blocks", uint64_t(info.blocks));
    w.field("headers", uint64_t(info.headers));
    w.field("bestblockhash", encode_hash(info.best_block_hash));
    w.field("difficulty", info.difficulty);
    if (info.progress) {
        w.field("ticks", info.progress->ticks);
        w.field("target_time", info.progress->target_time);
        w.field("next_target_time", info.progress->next_target_time);
    }
    w.field("mediantime", int64_t(info.median_time));
    w.field("verificationprogress", progress);
    w.field("initialblockdownload", info.initial_block_download);
    if (info.chain_work) {
        w.field("chainwork", encode_hash(*info.chain_work));
    }
    w.field("size_on_disk", info.size_on_disk);
    w.field("warnings", "");
    w.end_object();
    return w.str();
}

namespace {

double fee_per_kb(float byte_fee_satoshis) {
    auto const per_kb = std::llround(double(byte_fee_satoshis) * double(1_KB));
    return double(std::max(per_kb, 0LL)) / double(kth::satoshi_per_bitcoin);
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
    w.field("size", uint64_t(totals.size));
    w.field("bytes", uint64_t(totals.bytes));
    w.field("total_fee", uint64_t(totals.total_fee));
    w.end_object();
    return w.str();
}

std::string render_mempool_entry(blockchain::mempool_entry_info const& entry,
                                 hash_list const& depends, hash_list const& spentby) {
    writer w;
    w.begin_object();
    w.field("fee", uint64_t(entry.fee));
    w.field("size", uint64_t(entry.size));
    w.field("time", uint64_t(entry.time));
    w.key("depends");
    write_hash_array(w, depends);
    w.key("spentby");
    write_hash_array(w, spentby);
    w.end_object();
    return w.str();
}

std::string render_estimate_fee(float byte_fee_satoshis) {
    writer w;
    w.value(fee_per_kb(byte_fee_satoshis));
    return w.str();
}

std::string render_network_hash_ps(uint256_t const& work_before, uint256_t const& work_at_end,
                                   uint32_t min_time, uint32_t max_time) {
    writer w;
    if (max_time <= min_time || work_at_end < work_before) {
        w.value(0.0);
    } else {
        uint256_t const work = work_at_end - work_before;
        w.value(work.convert_to<double>() / double(max_time - min_time));
    }
    return w.str();
}

namespace {

// An output script's kind as the scriptPubKey "type", and the addresses it
// pays with the number of signatures that takes; no addresses for an
// unspendable or unrecognized script.
struct script_destination {
    std::string_view type;
    size_t required = 0;
    std::vector<std::string> addresses;
};

script_destination describe_output_script(domain::chain::script const& script,
                                          domain::config::network net) {
    using domain::machine::script_pattern;
    using domain::wallet::payment_address;
    auto const mainnet = net == domain::config::network::mainnet;
    auto const p2kh = mainnet ? payment_address::mainnet_p2kh : payment_address::testnet_p2kh;
    auto const p2sh = mainnet ? payment_address::mainnet_p2sh : payment_address::testnet_p2sh;

    script_destination out;
    switch (script.output_pattern()) {
        case script_pattern::pay_to_public_key_hash:
            out.type = "pubkeyhash";
            break;
        case script_pattern::pay_to_script_hash:
        case script_pattern::pay_to_script_hash_32:
            out.type = "scripthash";
            break;
        case script_pattern::pay_to_public_key:
            out.type = "pubkey";
            break;
        case script_pattern::pay_to_multisig: {
            // <m> <pubkey>... <n> checkmultisig
            out.type = "multisig";
            auto const ops = script.operations();
            out.required = size_t(uint8_t(ops.front().code()) -
                                       uint8_t(domain::machine::opcode::push_positive_1) + 1);
            for (auto it = ops.begin() + 1; it + 2 < ops.end(); ++it) {
                auto const address = payment_address::from_shorthash(bitcoin_short_hash(it->data()), p2kh);
                if (address) {
                    out.addresses.push_back(address->encoded_cashaddr(net));
                }
            }
            return out;
        }
        case script_pattern::null_data:
            out.type = "nulldata";
            return out;
        default:
            out.type = "nonstandard";
            return out;
    }
    for (auto const& address : payment_address::extract_output(script, p2kh, p2sh)) {
        out.addresses.push_back(address.encoded_cashaddr(net));
    }
    out.required = out.addresses.empty() ? 0 : 1;
    return out;
}

std::string_view capability_name(domain::chain::capability_t capability) {
    switch (capability) {
        case domain::chain::capability_t::mut:
            return "mutable";
        case domain::chain::capability_t::minting:
            return "minting";
        default:
            return "none";
    }
}

// A "scriptPubKey" object: asm, hex, and the kind with its addresses.
void write_script_pubkey(writer& w, domain::chain::script const& script,
                         domain::config::network net, domain::script_flags_t flags) {
    auto const destination = describe_output_script(script, net);
    w.key("scriptPubKey").begin_object();
    w.field("asm", script.to_string(flags));
    w.field("hex", encode_base16(script.bytes()));
    if ( ! destination.addresses.empty()) {
        w.field("reqSigs", uint64_t(destination.required));
    }
    w.field("type", destination.type);
    if ( ! destination.addresses.empty()) {
        w.key("addresses").begin_array();
        for (auto const& address : destination.addresses) {
            w.value(address);
        }
        w.end_array();
    }
    w.end_object();
}

// A "tokenData" object: category, fungible amount and the NFT, if any.
void write_token_data(writer& w, domain::chain::token_data_t const& token) {
    w.key("tokenData").begin_object();
    w.field("category", encode_hash(token.id));
    w.field("amount", std::to_string(domain::chain::get_amount(token)));
    if (domain::chain::has_nft(token)) {
        w.key("nft").begin_object();
        w.field("capability", capability_name(domain::chain::get_nft_capability(token)));
        w.field("commitment", encode_base16(domain::chain::get_nft_commitment(token)));
        w.end_object();
    }
    w.end_object();
}

} // namespace

namespace {

// The decoderawtransaction fields, into an object the caller has opened.
void write_transaction_fields(writer& w, domain::chain::transaction const& tx,
                              domain::config::network net, domain::script_flags_t flags) {
    w.field("txid", encode_hash(tx.hash()));
    w.field("hash", encode_hash(tx.hash()));
    w.field("version", int64_t(tx.version()));
    w.field("size", uint64_t(tx.serialized_size(true)));
    w.field("locktime", int64_t(tx.locktime()));

    w.key("vin").begin_array();
    for (auto const& input : tx.inputs()) {
        w.begin_object();
        if (tx.is_coinbase()) {
            w.field("coinbase", encode_base16(input.script().bytes()));
        } else {
            w.field("txid", encode_hash(input.previous_output().hash()));
            w.field("vout", int64_t(input.previous_output().index()));
            w.key("scriptSig").begin_object();
            w.field("asm", input.script().to_string(flags));
            w.field("hex", encode_base16(input.script().bytes()));
            w.end_object();
        }
        w.field("sequence", int64_t(input.sequence()));
        w.end_object();
    }
    w.end_array();

    w.key("vout").begin_array();
    uint64_t n = 0;
    for (auto const& output : tx.outputs()) {
        w.begin_object();
        w.field_raw("value", satoshi_to_bitcoin(output.value()));
        w.field("n", n++);
        write_script_pubkey(w, output.script(), net, flags);
        if (auto const& token = output.token_data()) {
            write_token_data(w, *token);
        }
        w.end_object();
    }
    w.end_array();
}

} // namespace

std::string render_transaction(domain::chain::transaction const& tx,
                               domain::config::network net, domain::script_flags_t flags) {
    writer w;
    w.begin_object();
    write_transaction_fields(w, tx, net, flags);
    w.end_object();
    return w.str();
}

std::string render_transaction_verbose(domain::chain::transaction const& tx,
                                       domain::config::network net, domain::script_flags_t flags,
                                       std::optional<transaction_location> const& location) {
    writer w;
    w.begin_object();
    write_transaction_fields(w, tx, net, flags);
    w.field("hex", transaction_to_hex(tx));
    if (location) {
        auto const active = location->confirmations >= 0;
        w.field("blockhash", encode_hash(location->block_hash));
        if (active) {
            w.field("confirmations", location->confirmations);
            if (location->confirmations_ticks) {
                w.field("confirmations_ticks", *location->confirmations_ticks);
            }
            w.field("time", int64_t(location->block_time));
            w.field("blocktime", int64_t(location->block_time));
        } else {
            w.field("confirmations", int64_t(0));
            if (location->confirmations_ticks) {
                w.field("confirmations_ticks", int64_t(0));
            }
        }
        w.field("in_active_chain", active);
    }
    w.end_object();
    return w.str();
}

std::string render_block_verbose(domain::chain::block const& block, block_location const& location,
                                 bool with_transactions,
                                 domain::config::network net, domain::script_flags_t flags) {
    auto const& header = block.header();
    writer w;
    w.begin_object();
    w.field("hash", encode_hash(block.hash()));
    w.field("confirmations", location.confirmations);
    if (location.confirmations_ticks) {
        w.field("confirmations_ticks", *location.confirmations_ticks);
    }
    w.field("size", uint64_t(block.serialized_size()));
    w.field("height", uint64_t(location.height));
    if (location.progress) {
        w.field("ticks", location.progress->ticks);
        w.field("target_time", location.progress->target_time);
        w.field("next_target_time", location.progress->next_target_time);
    }
    w.field("version", int64_t(header.version()));
    w.field("versionHex", fmt::format("{:08x}", header.version()));
    w.field("merkleroot", encode_hash(header.merkle()));
    w.key("tx").begin_array();
    for (auto const& tx : block.transactions()) {
        if (with_transactions) {
            w.begin_object();
            write_transaction_fields(w, tx, net, flags);
            w.field("hex", transaction_to_hex(tx));
            w.end_object();
        } else {
            w.value(encode_hash(tx.hash()));
        }
    }
    w.end_array();
    w.field("time", int64_t(header.timestamp()));
    w.field("mediantime", int64_t(location.median_time));
    w.field("nonce", int64_t(header.nonce()));
    w.field("bits", encode_base16(header.bits()));
    w.field("difficulty", blockchain::difficulty_from_bits(header.bits()));
    if (location.chain_work) {
        w.field("chainwork", encode_hash(*location.chain_work));
    }
    w.field("nTx", uint64_t(block.transactions().size()));
    if (header.previous_block_hash() != null_hash) {
        w.field("previousblockhash", encode_hash(header.previous_block_hash()));
    }
    if (location.next_block_hash) {
        w.field("nextblockhash", encode_hash(*location.next_block_hash));
    }
    w.end_object();
    return w.str();
}

namespace {

constexpr std::string_view tip_status_name(blockchain::header_index::tip_status status) noexcept {
    using enum blockchain::header_index::tip_status;
    switch (status) {
        case active: return "active";
        case valid_headers: return "valid-headers";
        case headers_only: return "headers-only";
    }
    std::unreachable();
}

} // namespace

std::string render_chain_tips(std::vector<chain_tip> const& tips) {
    writer w;
    w.begin_array();
    for (auto const& tip : tips) {
        w.begin_object();
        w.field("height", uint64_t(tip.height));
#if defined(KTH_CURRENCY_BCH)
        w.field("ticks", tip.ticks);
#endif
        w.field("hash", encode_hash(tip.hash));
        w.field("branchlen", uint64_t(tip.branch_length));
        w.field("status", tip_status_name(tip.status));
        w.end_object();
    }
    w.end_array();
    return w.str();
}

std::string render_tx_out(hash_digest const& best_block, int64_t confirmations,
                          domain::chain::output const& output, bool coinbase,
                          domain::config::network net, domain::script_flags_t flags,
                          std::optional<int64_t> confirmations_ticks) {
    writer w;
    w.begin_object();
    w.field("bestblock", encode_hash(best_block));
    w.field("confirmations", confirmations);
    if (confirmations_ticks) w.field("confirmations_ticks", *confirmations_ticks);
    w.field_raw("value", satoshi_to_bitcoin(output.value()));
    write_script_pubkey(w, output.script(), net, flags);
    if (auto const& token = output.token_data()) {
        write_token_data(w, *token);
    }
    w.field("coinbase", coinbase);
    w.end_object();
    return w.str();
}

namespace {

// "1.5.2" -> 1050200, the integer clients compare node versions with. A part
// that is missing or not a number counts as 0.
uint64_t version_number(std::string_view version) {
    uint64_t number = 0;
    for (uint64_t const scale : {1000000u, 10000u, 100u}) {
        uint64_t part = 0;
        auto const [end, ec] = std::from_chars(version.data(), version.data() + version.size(), part);
        if (ec != std::errc{}) {
            break;
        }
        number += part * scale;
        version.remove_prefix(size_t(end - version.data()));
        if (version.empty() || version.front() != '.') {
            break;
        }
        version.remove_prefix(1);
    }
    return number;
}

// The names of the service bits in `services`, by bit order.
std::vector<std::string> service_names(uint64_t services) {
    using service = domain::message::version::service;
    std::vector<std::string> names;
    for (unsigned bit = 0; bit < 64; ++bit) {
        auto const flag = uint64_t(1) << bit;
        if ((services & flag) == 0) {
            continue;
        }
        switch (flag) {
            case service::node_network: names.emplace_back("NETWORK"); break;
            case service::node_getutxo: names.emplace_back("GETUTXO"); break;
            case service::node_bloom: names.emplace_back("BLOOM"); break;
#if defined(KTH_CURRENCY_BCH)
            case service::node_xthin: names.emplace_back("XTHIN"); break;
            case service::node_bitcoin_cash: names.emplace_back("BITCOIN_CASH"); break;
            case service::node_graphene: names.emplace_back("GRAPHENE"); break;
            case service::node_compact_filters: names.emplace_back("CF"); break;
            case service::node_extversion: names.emplace_back("EXTVERSION"); break;
#endif
            case service::node_network_limited: names.emplace_back("NETWORK_LIMITED"); break;
            default: names.push_back(fmt::format("UNKNOWN[2^{}]", bit)); break;
        }
    }
    return names;
}

} // namespace

std::string render_network_info(network_info const& info) {
    writer w;
    w.begin_object();
    w.field("version", version_number(info.version));
    w.field("subversion", info.subversion);
    w.field("protocolversion", uint64_t(info.protocol_version));
    w.field("localservices", fmt::format("{:016x}", info.services));
    w.key("localservicesnames").begin_array();
    for (auto const& name : service_names(info.services)) {
        w.value(name);
    }
    w.end_array();
    w.field("localrelay", info.relay);
    w.field("networkactive", info.active);
    w.field("connections", uint64_t(info.connections));
    w.field("relayfee", fee_per_kb(info.byte_fee_satoshis));
    w.field("warnings", "");
    w.end_object();
    return w.str();
}

std::string render_raw_mempool_verbose(std::vector<mempool_listing> const& listings) {
    writer w;
    w.begin_object();
    for (auto const& listing : listings) {
        w.key(encode_hash(listing.txid)).begin_object();
        auto const fee = satoshi_to_bitcoin(listing.fee);
        w.key("fees").begin_object();
        w.field_raw("base", fee);
        w.field_raw("modified", fee);
        w.end_object();
        w.field("size", uint64_t(listing.size));
        w.field("time", listing.time);
        w.key("depends");
        write_hash_array(w, listing.depends);
        w.key("spentby");
        write_hash_array(w, listing.spentby);
        w.end_object();
    }
    w.end_object();
    return w.str();
}

std::string header_to_hex(domain::chain::header const& header) {
    return encode_base16(to_data_chunk(header, /*wire*/ true));
}

std::string render_block_header(domain::chain::header const& header,
                                size_t height, hash_digest const& hash,
                                int64_t confirmations,
                                std::optional<chain_progress> progress,
                                std::optional<int64_t> confirmations_ticks) {
    writer w;
    w.begin_object();
    w.field("hash", encode_hash(hash));
    w.field("confirmations", confirmations);
    if (confirmations_ticks) w.field("confirmations_ticks", *confirmations_ticks);
    w.field("height", uint64_t(height));
    if (progress) {
        w.field("ticks", progress->ticks);
        w.field("target_time", progress->target_time);
        w.field("next_target_time", progress->next_target_time);
    }
    w.field("version", int64_t(header.version()));
    w.field("previousblockhash", encode_hash(header.previous_block_hash()));
    w.field("merkleroot", encode_hash(header.merkle()));
    w.field("time", int64_t(header.timestamp()));
    w.field("bits", encode_base16(header.bits()));
    w.field("nonce", int64_t(header.nonce()));
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
