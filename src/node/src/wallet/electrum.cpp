// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/wallet/electrum.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <utility>
#include <vector>

#include <asio/as_tuple.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/deferred.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/read_until.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <simdjson.h>
#include <spdlog/spdlog.h>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/node/wallet/blocks.hpp>
#if defined(KTH_WITH_RPC)
#include <kth/node/rpc/error.hpp>
#include <kth/node/rpc/json.hpp>
#include <kth/node/rpc/query.hpp>
#endif

namespace kth::node::wallet {

namespace {

using namespace std::chrono_literals;
using tcp = ::asio::ip::tcp;
namespace chain = domain::chain;

constexpr std::string_view protocol_version = "1.5";
constexpr std::string_view software = "Knuth wallet support";
constexpr size_t max_line = 4 * 1024 * 1024;
// What a client follows is checked when something changes, and at least this often.
constexpr auto notify_wait = std::chrono::seconds(60);

// JSON-RPC error codes, as Fulcrum answers them: the node's RPC codes, and
// Fulcrum's own for a request it cannot answer.
constexpr int invalid_params = int(rpc::error_code::invalid_params);
constexpr int method_not_found = int(rpc::error_code::method_not_found);
constexpr int internal_error = int(rpc::error_code::internal_error);
constexpr int bad_request = 1;

// Fulcrum's RPA defaults: the shortest prefix answered, and the blocks one
// blockchain.rpa.get_history call reads at most (rpa_history_blocks).
constexpr size_t rpa_prefix_bits_min = 8;
constexpr uint32_t rpa_history_blocks = 60;
constexpr std::string_view rpa_prefix_refusal = "Invalid prefix argument; expected hex string of at least 2 and at most 4 characters";

struct failure {
    int code;
    std::string message;
};

using result = std::expected<std::string, failure>;

std::unexpected<failure> fail(int code, std::string message) {
    return std::unexpected(failure{code, std::move(message)});
}

// ---------------------------------------------------------------------------
// JSON text, written with the node's JSON-RPC writer

std::string json_text(std::string_view text) {
    rpc::writer out;
    out.value(text);
    return out.str();
}

// One JSON object, a field at a time.
struct object {
    object() { out_.begin_object(); }
    object& raw(std::string_view key, std::string_view value) {
        out_.field_raw(key, value);
        return *this;
    }
    object& text(std::string_view key, std::string_view value) {
        out_.key(key);
        out_.value(value);
        return *this;
    }
    object& number(std::string_view key, int64_t value) {
        out_.key(key);
        out_.value(value);
        return *this;
    }
    object& flag(std::string_view key, bool value) {
        out_.key(key);
        out_.value(value);
        return *this;
    }
    std::string done() {
        out_.end_object();
        return out_.str();
    }
private:
    rpc::writer out_;
};

std::string array_of(std::vector<std::string> const& items) {
    rpc::writer out;
    out.begin_array();
    for (auto const& item : items) out.value_raw(item);
    out.end_array();
    return out.str();
}

std::string coins(uint64_t satoshis) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.8f", double(satoshis) / 100000000.0);
    return text;
}

// ---------------------------------------------------------------------------
// Parameters

struct param {
    enum class kind { null, boolean, number, string, other };
    kind what = kind::other;
    bool flag = false;
    int64_t number = 0;
    std::string text;
};

std::optional<std::vector<param>> params_of(std::string_view raw) {
    std::vector<param> out;
    if (raw.empty()) return out;
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(raw);
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) return std::nullopt;
    simdjson::ondemand::array items;
    if (doc.get_array().get(items) != simdjson::SUCCESS) return std::nullopt;
    for (auto element : items) {
        param p;
        simdjson::ondemand::value value;
        simdjson::ondemand::json_type type;
        if (element.get(value) != simdjson::SUCCESS || value.type().get(type) != simdjson::SUCCESS) return std::nullopt;
        if (type == simdjson::ondemand::json_type::string) {
            std::string_view text;
            if (value.get_string().get(text) != simdjson::SUCCESS) return std::nullopt;
            p.what = param::kind::string;
            p.text.assign(text);
        } else if (type == simdjson::ondemand::json_type::number) {
            int64_t number = 0;
            if (value.get_int64().get(number) != simdjson::SUCCESS) {
                double real = 0;
                if (value.get_double().get(real) != simdjson::SUCCESS) return std::nullopt;
                number = int64_t(real);
            }
            p.what = param::kind::number;
            p.number = number;
        } else if (type == simdjson::ondemand::json_type::boolean) {
            if (value.get_bool().get(p.flag) != simdjson::SUCCESS) return std::nullopt;
            p.what = param::kind::boolean;
        } else if (type == simdjson::ondemand::json_type::null) {
            p.what = param::kind::null;
        } else {
            p.what = param::kind::other;
        }
        out.push_back(std::move(p));
    }
    return out;
}

std::optional<std::vector<std::string>> batch_of(std::string_view line) {
    simdjson::ondemand::parser parser;
    simdjson::padded_string padded(line);
    simdjson::ondemand::document doc;
    if (parser.iterate(padded).get(doc) != simdjson::SUCCESS) return std::nullopt;
    simdjson::ondemand::array items;
    if (doc.get_array().get(items) != simdjson::SUCCESS) return std::nullopt;
    std::vector<std::string> out;
    for (auto element : items) {
        simdjson::ondemand::value value;
        std::string_view raw;
        if (element.get(value) != simdjson::SUCCESS || value.raw_json().get(raw) != simdjson::SUCCESS) return std::nullopt;
        out.emplace_back(raw);
    }
    return out;
}

std::string trimmed(std::string_view raw) {
    while ( ! raw.empty() && (raw.back() == ' ' || raw.back() == '\n' || raw.back() == '\r' || raw.back() == '\t')) raw.remove_suffix(1);
    while ( ! raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    return std::string(raw);
}

// ---------------------------------------------------------------------------
// Values

data_chunk header_bytes(chain::header const& header) {
    data_chunk bytes(80);
    byte_writer writer(bytes);
    (void)header.to_data(writer, true);
    return bytes;
}

data_chunk transaction_bytes(chain::transaction const& tx) {
    data_chunk bytes(tx.serialized_size(true));
    byte_writer writer(bytes);
    (void)tx.to_data(writer, true);
    return bytes;
}

std::string token_json(chain::token_data_t const& token) {
    object out;
    out.text("category", encode_hash(token.id));
    std::visit([&](auto const& data) {
        using kind = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<kind, chain::fungible>) {
            out.text("amount", std::to_string(uint64_t(data.amount)));
        } else if constexpr (std::is_same_v<kind, chain::non_fungible>) {
            out.text("amount", "0");
        } else {
            out.text("amount", std::to_string(uint64_t(data.fungible_part.amount)));
        }
        auto const nft = [&](chain::non_fungible const& part) {
            object item;
            item.text("capability", part.capability == chain::capability_t::mut ? "mutable"
                : part.capability == chain::capability_t::minting ? "minting" : "none");
            item.text("commitment", encode_base16(part.commitment));
            out.raw("nft", item.done());
        };
        if constexpr (std::is_same_v<kind, chain::non_fungible>) nft(data);
        if constexpr (std::is_same_v<kind, chain::both_kinds>) nft(data.non_fungible_part);
    }, token.data);
    return out.done();
}

std::string supply_json(token_supply const& supply) {
    // Amounts as text, as token_data's: they reach 2^63 - 1.
    return object().text("total", std::to_string(supply.total)).text("reserved", std::to_string(supply.reserved))
        .text("circulating", std::to_string(supply.circulating)).done();
}

std::string unspent_json(utxo const& coin) {
    object item;
    item.text("tx_hash", encode_hash(coin.outpoint.hash())).number("tx_pos", coin.outpoint.index())
        .number("height", coin.height.value_or(0)).number("value", int64_t(coin.value));
    if (coin.token) item.raw("token_data", token_json(*coin.token));
    return item.done();
}

std::string category_json(category_state const& found) {
    object out;
    out.text("category", encode_hash(found.category));
    if (found.genesis) {
        out.raw("genesis", object().text("tx_hash", encode_hash(*found.genesis))
            .number("height", found.genesis_height.value_or(0)).done());
    } else {
        out.raw("genesis", "null");
    }
    out.raw("supply", supply_json(found.supply)).raw("mempool_supply", supply_json(found.mempool_supply));
    out.number("holders", int64_t(found.holders)).number("nfts", int64_t(found.nfts));
    std::vector<std::string> unspent;
    for (auto const& coin : found.unspent) unspent.push_back(unspent_json(coin));
    out.raw("unspent", array_of(unspent));
    return out.done();
}

std::string publication_json(bcmr_publication const& found) {
    std::vector<std::string> uris;
    for (auto const& uri : found.uris) uris.push_back(json_text(uri));
    // The hash as pushed: OP_SHA256 byte order, what sha256sum prints.
    return object().number("tx_pos", found.output).text("hash", encode_base16(found.registry_hash))
        .raw("uris", array_of(uris)).done();
}

std::string identity_json(identity const& found) {
    object out;
    out.text("authbase", encode_hash(found.authbase));
    out.text("authhead", encode_hash(found.chain.empty() ? found.authbase : found.chain.back().txid));
    out.flag("burned", found.burned);
    std::vector<std::string> steps;
    for (auto const& step : found.chain) {
        auto const source = step.from == identity_step::source::chain ? "chain"
            : step.from == identity_step::source::mempool ? "mempool" : "registry";
        steps.push_back(object().text("tx_hash", encode_hash(step.txid)).number("height", step.height)
            .text("source", source).done());
    }
    out.raw("chain", array_of(steps));
    out.raw("publication", found.publication ? publication_json(*found.publication) : std::string("null"));
    std::vector<std::string> ignored;
    for (auto const& item : found.ignored) ignored.push_back(publication_json(item));
    out.raw("ignored", array_of(ignored));
    return out.done();
}

std::string header_json(uint32_t height, chain::header const& header) {
    return object().number("height", height).text("hex", encode_base16(header_bytes(header))).done();
}

std::string hashes_json(hash_list const& hashes) {
    std::vector<std::string> items;
    for (auto const& hash : hashes) items.push_back(json_text(encode_hash(hash)));
    return array_of(items);
}

std::string dsproof_json(dsproof_info const& proof) {
    object out;
    out.text("dspid", encode_hash(proof.dspid)).text("txid", encode_hash(proof.txid)).text("hex", encode_base16(proof.raw));
    out.raw("outpoint", object().text("txid", encode_hash(proof.outpoint.hash())).number("vout", proof.outpoint.index()).done());
    hash_list descendants(proof.descendants.begin(), proof.descendants.end());
    out.raw("descendants", hashes_json(descendants));
    return out.done();
}

} // namespace

// ---------------------------------------------------------------------------

struct electrum_server::impl : std::enable_shared_from_this<electrum_server::impl> {
    service& wallet;
    domain::config::network network;
    tcp::acceptor acceptor;
    std::mutex sockets_mutex;
    std::list<std::shared_ptr<tcp::socket>> sockets;
    std::atomic<bool> stopping{false};

    impl(service& owner, domain::config::network net)
        : wallet(owner), network(net), acceptor(owner.executor()) {}

    // A script hash given as the Electrum protocol writes it: SHA-256 of the
    // script, bytes reversed.
    static std::optional<target> script_hash(param const& p) {
        if (p.what != param::kind::string || p.text.size() != 64) return std::nullopt;
        auto const hash = decode_hash(p.text);
        if ( ! hash) return std::nullopt;
        return target::of_script_hash(*hash);
    }

    std::optional<data_chunk> address_script(param const& p) const {
        if (p.what != param::kind::string) return std::nullopt;
        auto const parsed = domain::wallet::payment_address::parse_from(p.text, network);
        if ( ! parsed) return std::nullopt;
        auto const version = parsed->version();
        if (parsed->hash_span().size() == hash_size) {
            return chain::script(chain::script::to_pay_script_hash_32_pattern(parsed->hash32())).bytes();
        }
        if (version == domain::wallet::payment_address::mainnet_p2sh || version == domain::wallet::payment_address::testnet_p2sh) {
            return chain::script(chain::script::to_pay_script_hash_pattern(parsed->hash20())).bytes();
        }
        return chain::script(chain::script::to_pay_public_key_hash_pattern(parsed->hash20())).bytes();
    }

    std::optional<target> target_of(std::string_view method, std::vector<param> const& params) const {
        if (params.empty()) return std::nullopt;
        if (method.starts_with("blockchain.address.")) {
            auto const script = address_script(params[0]);
            if ( ! script) return std::nullopt;
            return target::of_script(*script);
        }
        return script_hash(params[0]);
    }

    static std::optional<token_filter> tokens_of(std::vector<param> const& params, size_t at) {
        if (params.size() <= at) return token_filter::include;
        if (params[at].what != param::kind::string) return std::nullopt;
        auto text = params[at].text;
        std::ranges::transform(text, text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (text == "include_tokens") return token_filter::include;
        if (text == "exclude_tokens") return token_filter::exclude;
        if (text == "tokens_only") return token_filter::only;
        return std::nullopt;
    }

    // from_height (inclusive) and to_height (exclusive, -1 or absent for
    // the mempool too), as Fulcrum reads them.
    static std::optional<std::pair<uint32_t, std::optional<uint32_t>>> range_of(std::vector<param> const& params, size_t at) {
        uint32_t from = 0;
        std::optional<uint32_t> to;
        if (params.size() > at) {
            if (params[at].what != param::kind::number || params[at].number < 0) return std::nullopt;
            from = uint32_t(params[at].number);
        }
        if (params.size() > at + 1) {
            if (params[at + 1].what != param::kind::number) return std::nullopt;
            auto const value = params[at + 1].number;
            if (value < -1) return std::nullopt;
            if (value >= 0) to = uint32_t(value);
            if (to && from > *to) return std::nullopt;
        }
        return std::pair{from, to};
    }

    static std::optional<hash_digest> txid_of(std::vector<param> const& params, size_t at) {
        if (params.size() <= at || params[at].what != param::kind::string) return std::nullopt;
        auto const hash = decode_hash(params[at].text);
        if ( ! hash) return std::nullopt;
        return *hash;
    }

    // At least rpa_prefix_bits_min bits (8, Fulcrum's default), at most 16.
    static std::optional<std::pair<uint16_t, uint8_t>> prefix_of(param const& p) {
        if (p.what != param::kind::string || p.text.size() < rpa_prefix_bits_min / 4 || p.text.size() > 4) return std::nullopt;
        uint32_t value = 0;
        auto const [end, ec] = std::from_chars(p.text.data(), p.text.data() + p.text.size(), value, 16);
        if (ec != std::errc{} || end != p.text.data() + p.text.size()) return std::nullopt;
        return std::pair{uint16_t(value), uint8_t(p.text.size() * 4)};
    }

    static std::string history_item(hash_digest const& txid, int64_t height, std::optional<uint64_t> fee) {
        object item;
        item.text("tx_hash", encode_hash(txid)).number("height", height);
        if (fee) item.number("fee", int64_t(*fee));
        return item.done();
    }

    // Confirmed in [from, to), then, without a to_height, the mempool.
    static std::vector<std::string> history_items(target_history const& history, uint32_t from, std::optional<uint32_t> to, bool mempool_only) {
        std::vector<std::string> items;
        if ( ! mempool_only) {
            for (auto const& tx : history.confirmed) {
                if (tx.height < from || (to && tx.height >= *to)) continue;
                items.push_back(history_item(tx.txid, tx.height, std::nullopt));
            }
        }
        if (mempool_only || ! to) {
            for (auto const& tx : history.unconfirmed) {
                items.push_back(history_item(tx.txid, tx.unconfirmed_parent ? -1 : 0, tx.fee));
            }
        }
        return items;
    }

    // SHA-256 of "txid:height:" for every transaction, or null for none.
    // A connected client, and what it follows (defined with the clients below).
    struct client;

    static std::string status_of(target_history const& history) {
        std::string text;
        for (auto const& tx : history.confirmed) text += encode_hash(tx.txid) + ":" + std::to_string(tx.height) + ":";
        for (auto const& tx : history.unconfirmed) text += encode_hash(tx.txid) + ":" + std::to_string(tx.unconfirmed_parent ? -1 : 0) + ":";
        if (text.empty()) return "null";
        auto const digest = sha256_hash(byte_span(reinterpret_cast<uint8_t const*>(text.data()), text.size()));
        return json_text(encode_base16(digest));
    }

    ::asio::awaitable<std::optional<std::pair<uint32_t, chain::header>>> tip() {
        // The tip the answers reach, as Fulcrum shows only what it has
        // indexed: on a full node, the connected tip.
        auto const now = wallet.current();
        auto const height = now.tip.value_or(0);
        auto const header = co_await wallet.header(height);
        if ( ! header) co_return std::nullopt;
        co_return std::pair{height, *header};
    }

    // One request. `who` asked it over a connection: what it subscribes to is
    // recorded as the very answer it gets, so a change after that answer is
    // a change to notify, never lost between two readings.
    ::asio::awaitable<result> call(std::string const& method, std::vector<param> const& params, client* who = nullptr) {
        // server.*
        if (method == "server.version") co_return array_of({json_text(software), json_text(protocol_version)});
        if (method == "server.ping") co_return "null";
        if (method == "server.banner") co_return json_text(std::string(software) + ": answered by this node's own wallet support");
        if (method == "server.donation_address") co_return json_text("");
        if (method == "server.peers.subscribe") co_return "[]";
        if (method == "server.add_peer") co_return "false";
        if (method == "server.features") {
            auto const genesis = co_await wallet.header(0);
            object out;
            out.text("genesis_hash", genesis ? encode_hash(domain::chain::hash(*genesis)) : std::string{});
            out.raw("hosts", "{}");
            out.text("protocol_min", "1.4").text("protocol_max", protocol_version);
            out.raw("pruning", "null").text("server_version", software).text("hash_function", "sha256");
            out.flag("cashtokens", true).flag("dsproof", true);
            out.raw("rpa", object().number("history_block_limit", rpa_history_blocks).number("max_history", 0).number("prefix_bits", 16)
                .number("prefix_bits_min", int64_t(rpa_prefix_bits_min)).number("starting_height", 0).done());
            co_return out.done();
        }

        // Headers
        if (method == "blockchain.headers.get_tip" || method == "blockchain.headers.subscribe") {
            auto const now = co_await tip();
            if ( ! now) co_return fail(internal_error, "no headers yet");
            auto shown = header_json(now->first, now->second);
            if (who != nullptr && method == "blockchain.headers.subscribe") who->tip = shown;
            co_return shown;
        }
        if (method == "blockchain.headers.unsubscribe") {
            if (who != nullptr) who->tip.reset();
            co_return "true";
        }
        if (method == "blockchain.block.header") {
            if (params.empty() || params[0].what != param::kind::number || params[0].number < 0) co_return fail(invalid_params, "Invalid height");
            auto const height = uint32_t(params[0].number);
            uint32_t cp_height = 0;
            if (params.size() > 1) {
                if (params[1].what != param::kind::number || params[1].number < 0) co_return fail(invalid_params, "Invalid cp_height");
                cp_height = uint32_t(params[1].number);
            }
            auto const header = co_await wallet.header(height);
            if ( ! header) co_return fail(invalid_params, "Invalid height");
            if (cp_height == 0) co_return json_text(encode_base16(header_bytes(*header)));
            auto const proof = co_await wallet.header_branch(height, cp_height);
            if ( ! proof) {
                co_return fail(invalid_params, "header height " + std::to_string(height) + " must be <= cp_height " +
                    std::to_string(cp_height) + " which must be <= chain height");
            }
            co_return object().text("header", encode_base16(header_bytes(*header))).raw("branch", hashes_json(proof->first))
                .text("root", encode_hash(proof->second)).done();
        }
        if (method == "blockchain.block.headers") {
            if (params.size() < 2 || params[0].what != param::kind::number || params[1].what != param::kind::number ||
                params[0].number < 0 || params[1].number < 0) co_return fail(invalid_params, "Invalid start or count");
            auto const start = uint32_t(params[0].number);
            auto const count = std::min<int64_t>(params[1].number, 2016);
            std::string hex;
            int64_t found = 0;
            for (int64_t i = 0; i < count; ++i) {
                auto const header = co_await wallet.header(start + uint32_t(i));
                if ( ! header) break;
                hex += encode_base16(header_bytes(*header));
                ++found;
            }
            object answer;
            answer.number("count", found).text("hex", hex).number("max", 2016);
            if (params.size() > 2 && params[2].what == param::kind::number && params[2].number > 0 && found > 0) {
                auto const cp_height = uint32_t(params[2].number);
                auto const proof = co_await wallet.header_branch(start + uint32_t(found) - 1, cp_height);
                if ( ! proof) co_return fail(invalid_params, "header height + (count - 1) must be <= cp_height which must be <= chain height");
                answer.raw("branch", hashes_json(proof->first)).text("root", encode_hash(proof->second));
            }
            co_return answer.done();
        }
        if (method == "blockchain.header.get") {
            if (params.empty()) co_return fail(invalid_params, "Invalid height or hash");
            if (params[0].what == param::kind::number && params[0].number >= 0) {
                auto const header = co_await wallet.header(uint32_t(params[0].number));
                if ( ! header) co_return fail(invalid_params, "Invalid height");
                co_return header_json(uint32_t(params[0].number), *header);
            }
            co_return fail(invalid_params, "Invalid height");
        }

        // Fees and mempool
        if (method == "blockchain.relayfee") co_return coins((co_await wallet.fees()).relay);
        if (method == "blockchain.estimatefee") co_return coins((co_await wallet.fees()).estimate);
        if (method == "mempool.get_info") {
            auto const fees = co_await wallet.fees();
            co_return object().raw("mempoolminfee", coins(fees.relay)).raw("minrelaytxfee", coins(fees.relay))
                .raw("incrementalrelayfee", coins(fees.relay)).done();
        }
        if (method == "mempool.get_fee_histogram") {
            auto summary = co_await wallet.mempool();
            std::ranges::sort(summary.fee_rates, [](auto const& a, auto const& b) { return a.first > b.first; });
            std::vector<std::string> bins;
            double bin_size = 100'000;
            size_t size = 0;
            double previous = 0;
            for (auto const& [rate, bytes] : summary.fee_rates) {
                if (size > bin_size && rate < previous) {
                    bins.push_back(array_of({std::to_string(int64_t(previous)), std::to_string(size)}));
                    size = 0;
                    bin_size *= 1.1;
                }
                size += bytes;
                previous = rate;
            }
            if (size > 0) bins.push_back(array_of({std::to_string(int64_t(previous)), std::to_string(size)}));
            co_return array_of(bins);
        }

        // Script hashes and addresses
        if (method.starts_with("blockchain.scripthash.") || method.starts_with("blockchain.address.")) {
            auto const call_name = method.substr(method.find('.', std::string_view("blockchain.").size()) + 1);
            if (call_name == "get_scripthash") {
                if (params.empty()) co_return fail(invalid_params, "Invalid address");
                auto const script = address_script(params[0]);
                if ( ! script) co_return fail(invalid_params, "Invalid address");
                co_return json_text(encode_hash(sha256_hash(*script)));
            }
            auto const watched = target_of(method, params);
            if ( ! watched) co_return fail(invalid_params, method.starts_with("blockchain.address.") ? "Invalid address" : "Invalid scripthash");
            if (call_name == "get_balance") {
                auto const tokens = tokens_of(params, 1);
                if ( ! tokens) co_return fail(invalid_params, "Invalid token filtering option. Specify one of: \"exclude_tokens\", \"include_tokens\", \"tokens_only\"");
                auto const answer = co_await wallet.get_balance(*watched, *tokens);
                if ( ! answer) co_return fail(internal_error, answer.error().message());
                co_return object().number("confirmed", int64_t(answer->confirmed)).number("unconfirmed", answer->unconfirmed).done();
            }
            if (call_name == "get_history" || call_name == "get_mempool") {
                auto const range = range_of(params, 1);
                if ( ! range) co_return fail(invalid_params, "Bad from_height or to_height argument");
                auto const answer = co_await wallet.history(*watched);
                if ( ! answer) co_return fail(internal_error, answer.error().message());
                co_return array_of(history_items(*answer, range->first, range->second, call_name == "get_mempool"));
            }
            if (call_name == "get_status" || call_name == "subscribe") {
                auto const answer = co_await wallet.history(*watched);
                if ( ! answer) co_return fail(internal_error, answer.error().message());
                auto status = status_of(*answer);
                if (who != nullptr && call_name == "subscribe") who->scripts[params[0].text] = status;
                co_return status;
            }
            if (call_name == "unsubscribe") {
                if (who != nullptr) who->scripts.erase(params[0].text);
                co_return "true";
            }
            if (call_name == "get_first_use") {
                auto const answer = co_await wallet.history(*watched);
                if ( ! answer) co_return fail(internal_error, answer.error().message());
                if ( ! answer->confirmed.empty()) {
                    auto const& first = answer->confirmed.front();
                    co_return object().text("block_hash", encode_hash(first.block)).number("block_height", first.height)
                        .number("height", first.height).text("tx_hash", encode_hash(first.txid)).done();
                }
                if ( ! answer->unconfirmed.empty()) {
                    co_return object().text("block_hash", encode_hash(null_hash)).number("block_height", 0)
                        .number("height", 0).text("tx_hash", encode_hash(answer->unconfirmed.front().txid)).done();
                }
                co_return "null";
            }
            if (call_name == "listunspent") {
                auto const tokens = tokens_of(params, 1);
                if ( ! tokens) co_return fail(invalid_params, "Invalid token filtering option. Specify one of: \"exclude_tokens\", \"include_tokens\", \"tokens_only\"");
                auto const answer = co_await wallet.unspent(*watched, *tokens);
                if ( ! answer) co_return fail(internal_error, answer.error().message());
                std::vector<std::string> items;
                for (auto const& coin : *answer) {
                    object item;
                    item.text("tx_hash", encode_hash(coin.outpoint.hash())).number("tx_pos", coin.outpoint.index())
                        .number("height", coin.height.value_or(0)).number("value", int64_t(coin.value));
                    if (coin.token) item.raw("token_data", token_json(*coin.token));
                    items.push_back(item.done());
                }
                co_return array_of(items);
            }
            co_return fail(method_not_found, "Unknown method");
        }

        // Transactions
        if (method == "blockchain.transaction.broadcast") {
            if (params.empty() || params[0].what != param::kind::string) co_return fail(invalid_params, "Invalid transaction");
            auto const raw = decode_base16(params[0].text);
            if ( ! raw) co_return fail(invalid_params, "Invalid transaction hex");
            byte_reader reader(*raw);
            auto tx = chain::transaction::from_data(reader, true);
            if ( ! tx || reader.remaining_size() != 0) co_return fail(invalid_params, "Invalid transaction");
            auto const sent = co_await wallet.broadcast(std::move(*tx));
            if ( ! sent) co_return fail(bad_request, "the transaction was not accepted: " + sent.error().message() + " " + wallet.current().error);
            co_return json_text(encode_hash(*sent));
        }
        if (method == "blockchain.transaction.get") {
            auto const txid = txid_of(params, 0);
            if ( ! txid) co_return fail(invalid_params, "Invalid tx hash");
            bool const verbose = params.size() > 1 && params[1].what == param::kind::boolean && params[1].flag;
            auto const found = co_await wallet.transaction(*txid, std::nullopt);
            if ( ! found) co_return fail(bad_request, "No such mempool or blockchain transaction");
            auto const raw = transaction_bytes(found->tx);
            if ( ! verbose) co_return json_text(encode_base16(raw));
#if defined(KTH_WITH_RPC)
            {
                // Fulcrum answers with its daemon's verbose getrawtransaction:
                // kth's own is the same object.
                std::optional<rpc::transaction_location> location;
                if (found->confirmed) {
                    auto const now = wallet.current();
                    auto const tip_height = now.tip.value_or(found->confirmed->height);
                    auto const header = co_await wallet.header(found->confirmed->height);
                    // Found by its hash alone: no block was named, so no
                    // in_active_chain, as BCHN answers getrawtransaction <txid> true.
                    location = rpc::transaction_location{found->confirmed->block,
                        int64_t(tip_height) - int64_t(found->confirmed->height) + 1,
                        header ? header->timestamp() : 0u, std::nullopt, false};
                }
                co_return rpc::render_transaction_verbose(found->tx, network,
                    blockchain::settings(network).enabled_flags(), location);
            }
#endif
            object out;
            out.text("hex", encode_base16(raw)).text("txid", encode_hash(*txid)).text("hash", encode_hash(*txid));
            out.number("size", int64_t(raw.size())).number("version", found->tx.version()).number("locktime", found->tx.locktime());
            std::vector<std::string> vin;
            for (auto const& in : found->tx.inputs()) {
                object item;
                if (found->tx.is_coinbase()) {
                    item.text("coinbase", encode_base16(in.script().bytes()));
                } else {
                    item.text("txid", encode_hash(in.previous_output().hash())).number("vout", in.previous_output().index());
                    item.raw("scriptSig", object().text("hex", encode_base16(in.script().bytes())).done());
                }
                item.number("sequence", in.sequence());
                vin.push_back(item.done());
            }
            std::vector<std::string> vout;
            for (uint32_t n = 0; n < found->tx.outputs().size(); ++n) {
                auto const& out_item = found->tx.outputs()[n];
                object item;
                item.raw("value", coins(out_item.value())).number("n", n);
                item.raw("scriptPubKey", object().text("hex", encode_base16(out_item.script().bytes())).done());
                if (out_item.token_data()) item.raw("tokenData", token_json(*out_item.token_data()));
                vout.push_back(item.done());
            }
            out.raw("vin", array_of(vin)).raw("vout", array_of(vout));
            if (found->confirmed) {
                auto const now = wallet.current();
                auto const tip_height = now.tip.value_or(found->confirmed->height);
                out.text("blockhash", encode_hash(found->confirmed->block));
                out.number("confirmations", int64_t(tip_height) - int64_t(found->confirmed->height) + 1);
                auto const header = co_await wallet.header(found->confirmed->height);
                if (header) out.number("time", header->timestamp()).number("blocktime", header->timestamp());
            }
            co_return out.done();
        }
        if (method == "blockchain.transaction.get_height") {
            auto const txid = txid_of(params, 0);
            if ( ! txid) co_return fail(invalid_params, "Invalid tx hash");
            auto const found = co_await wallet.transaction(*txid, std::nullopt);
            if ( ! found) co_return "null";
            co_return std::to_string(found->confirmed ? found->confirmed->height : 0);
        }
        if (method == "blockchain.transaction.get_confirmed_blockhash") {
            auto const txid = txid_of(params, 0);
            if ( ! txid) co_return fail(invalid_params, "Invalid tx hash");
            auto const found = co_await wallet.transaction(*txid, std::nullopt);
            if ( ! found || ! found->confirmed) co_return fail(bad_request, "No confirmed transaction with that hash");
            object out;
            out.text("block_hash", encode_hash(found->confirmed->block)).number("block_height", found->confirmed->height);
            if (params.size() > 1 && params[1].what == param::kind::boolean && params[1].flag) {
                auto const header = co_await wallet.header(found->confirmed->height);
                if (header) out.text("block_header", encode_base16(header_bytes(*header)));
            }
            co_return out.done();
        }
        if (method == "blockchain.transaction.get_merkle") {
            auto const txid = txid_of(params, 0);
            if ( ! txid) co_return fail(invalid_params, "Invalid tx hash");
            std::optional<uint32_t> hint;
            if (params.size() > 1 && params[1].what == param::kind::number && params[1].number >= 0) hint = uint32_t(params[1].number);
            auto const proof = co_await wallet.merkle(*txid, hint);
            if ( ! proof) co_return fail(bad_request, "No confirmed transaction with that hash");
            std::vector<std::string> branch;
            for (auto const& hash : proof->branch) branch.push_back(json_text(encode_hash(hash)));
            co_return object().number("block_height", proof->confirmed.height).raw("merkle", array_of(branch))
                .number("pos", proof->confirmed.position).done();
        }
        if (method == "blockchain.transaction.id_from_pos") {
            if (params.size() < 2 || params[0].what != param::kind::number || params[1].what != param::kind::number ||
                params[0].number < 0 || params[1].number < 0) co_return fail(invalid_params, "Invalid height or position");
            bool const with_merkle = params.size() > 2 && params[2].what == param::kind::boolean && params[2].flag;
            auto const found = co_await wallet.transaction_at(uint32_t(params[0].number), uint32_t(params[1].number));
            if ( ! found) co_return fail(bad_request, "No transaction at that position");
            if ( ! with_merkle) co_return json_text(encode_hash(found->first.hash()));
            std::vector<std::string> branch;
            for (auto const& hash : found->second.branch) branch.push_back(json_text(encode_hash(hash)));
            co_return object().text("tx_hash", encode_hash(found->first.hash())).raw("merkle", array_of(branch)).done();
        }
        if (method == "blockchain.transaction.subscribe") {
            auto const txid = txid_of(params, 0);
            if ( ! txid) co_return fail(invalid_params, "Invalid tx hash");
            auto const found = co_await wallet.transaction(*txid, std::nullopt);
            auto height = found ? std::to_string(found->confirmed ? found->confirmed->height : 0) : std::string("null");
            if (who != nullptr) who->transactions[params[0].text] = height;
            co_return height;
        }
        if (method == "blockchain.transaction.unsubscribe") {
            if (who != nullptr && ! params.empty()) who->transactions.erase(params[0].text);
            co_return "true";
        }

        if (method == "blockchain.utxo.get_info") {
            auto const txid = txid_of(params, 0);
            if ( ! txid) co_return fail(invalid_params, "Invalid tx hash");
            if (params.size() < 2 || params[1].what != param::kind::number || params[1].number < 0 || params[1].number > 0xffffffffll) {
                co_return fail(invalid_params, "Invalid tx out number");
            }
            auto const found = co_await wallet.output(domain::chain::point(*txid, uint32_t(params[1].number)));
            if ( ! found) co_return fail(internal_error, found.error().message());
            if ( ! *found) co_return "null";
            auto const& coin = **found;
            object out;
            if (coin.height) out.number("confirmed_height", *coin.height);
            out.text("scripthash", encode_hash(sha256_hash(coin.script)));
            if (coin.token) out.raw("token_data", token_json(*coin.token));
            out.number("value", int64_t(coin.value));
            co_return out.done();
        }
        if (method == "blockchain.transaction.dsproof.get" || method == "blockchain.transaction.dsproof.subscribe") {
            auto const id = txid_of(params, 0);
            if ( ! id) co_return fail(invalid_params, "Invalid dsp hash or tx hash");
            auto const proof = co_await wallet.dsproof(*id);
            auto shown = proof ? dsproof_json(*proof) : std::string("null");
            if (who != nullptr && method == "blockchain.transaction.dsproof.subscribe") who->proofs[params[0].text] = shown;
            co_return shown;
        }
        if (method == "blockchain.transaction.dsproof.list") {
            auto const listed = co_await wallet.dsproof_transactions();
            co_return hashes_json(hash_list(listed.begin(), listed.end()));
        }
        if (method == "blockchain.transaction.dsproof.unsubscribe") {
            if (who != nullptr && ! params.empty()) who->proofs.erase(params[0].text);
            co_return "true";
        }

        // Reusable payment addresses
        if (method == "blockchain.rpa.get_history" || method == "blockchain.reusable.get_history") {
            std::vector<param> args = params;
            if (method == "blockchain.reusable.get_history") {
                // (from_height, count, prefix) -> (prefix, from_height, from_height + count)
                if (args.size() < 3) co_return fail(invalid_params, "Bad arguments");
                std::rotate(args.begin(), args.begin() + 2, args.begin() + 3);
                if (args[1].what == param::kind::number && args[2].what == param::kind::number && args[1].number >= 0 && args[2].number >= 0) {
                    args[2].number = args[1].number + args[2].number;
                } else {
                    co_return fail(invalid_params, "Bad count argument");
                }
            }
            if (args.empty()) co_return fail(invalid_params, std::string(rpa_prefix_refusal));
            auto const prefix = prefix_of(args[0]);
            if ( ! prefix) co_return fail(invalid_params, std::string(rpa_prefix_refusal));
            auto const range = range_of(args, 1);
            if ( ! range) co_return fail(invalid_params, "Bad from_height or to_height argument");
            if (range->second && *range->second <= range->first) co_return "[]";
            // As Fulcrum: at most rpa_history_blocks blocks from from_height,
            // and never the mempool (blockchain.rpa.get_mempool has it).
            auto last = range->first + (rpa_history_blocks - 1);
            if (range->second) last = std::min(last, *range->second - 1);
            auto const confirmed = co_await wallet.rpa_history(prefix->first, prefix->second, range->first, last);
            if ( ! confirmed) co_return fail(internal_error, confirmed.error().message());
            std::vector<std::string> items;
            for (auto const& match : *confirmed) items.push_back(history_item(match.txid, match.height.value_or(0), std::nullopt));
            co_return array_of(items);
        }
        if (method == "blockchain.rpa.get_mempool" || method == "blockchain.reusable.get_mempool") {
            if (params.empty()) co_return fail(invalid_params, std::string(rpa_prefix_refusal));
            auto const prefix = prefix_of(params[0]);
            if ( ! prefix) co_return fail(invalid_params, std::string(rpa_prefix_refusal));
            std::vector<std::string> items;
            auto const pending = co_await wallet.rpa_mempool(prefix->first, prefix->second);
            for (auto const& match : pending) items.push_back(history_item(match.txid, 0, std::nullopt));
            co_return array_of(items);
        }
        // kth's own methods, not Fulcrum's: CashTokens categories and BCMR
        // identities, rebuilt from the chain with no index.
        if (method == "kth.token.category") {
            auto const id = txid_of(params, 0);
            if ( ! id) co_return fail(invalid_params, "Invalid category");
            auto const found = co_await wallet.category(*id);
            if ( ! found) co_return fail(internal_error, found.error().message());
            co_return category_json(*found);
        }
        if (method == "kth.token.holdings") {
            if (params.empty() || params[0].what != param::kind::string) co_return fail(invalid_params, "Invalid scripthash or address");
            auto const watched = target_of(params[0].text.size() == 64 ? "blockchain.scripthash.get_history" : "blockchain.address.get_history", params);
            if ( ! watched) co_return fail(invalid_params, "Invalid scripthash or address");
            auto const held = co_await wallet.holdings(*watched);
            if ( ! held) co_return fail(internal_error, held.error().message());
            std::vector<std::string> items;
            for (auto const& one : *held) {
                items.push_back(object().text("category", encode_hash(one.category)).text("amount", std::to_string(one.fungible))
                    .number("nfts", int64_t(one.nfts)).done());
            }
            co_return array_of(items);
        }
        if (method == "kth.identity.resolve") {
            // The authbase, then a registry's authchain extension, if any, as
            // raw transactions from the authbase on.
            auto const authbase = txid_of(params, 0);
            if ( ! authbase) co_return fail(invalid_params, "Invalid authbase");
            std::vector<chain::transaction> extension;
            for (size_t at = 1; at < params.size(); ++at) {
                if (params[at].what != param::kind::string) co_return fail(invalid_params, "Invalid authchain transaction");
                auto const raw = decode_base16(params[at].text);
                if ( ! raw) co_return fail(invalid_params, "Invalid authchain transaction");
                byte_reader reader(*raw);
                auto tx = chain::transaction::from_data(reader, true);
                if ( ! tx || reader.remaining_size() != 0) co_return fail(invalid_params, "Invalid authchain transaction");
                extension.push_back(std::move(*tx));
            }
            auto const found = co_await wallet.resolve_identity(*authbase, std::move(extension));
            if ( ! found) {
                if (found.error() == error::bad_stream) co_return fail(bad_request, "The registry's authchain diverged from the chain");
                co_return fail(internal_error, found.error().message());
            }
            co_return identity_json(*found);
        }
        if (method == "daemon.passthrough") co_return fail(method_not_found, "Method disabled for this client");
        co_return fail(method_not_found, "Unknown method");
    }

    ::asio::awaitable<std::string> answer_one(std::string const& line, client* who = nullptr) {
        auto const parsed = rpc::parse_request(line);
        if ( ! parsed) {
            co_return object().text("jsonrpc", "2.0").raw("error", object().number("code", parsed.error().code)
                .text("message", parsed.error().message).done()).raw("id", "null").done();
        }
        auto const params = params_of(parsed->params);
        result outcome = fail(invalid_params, "Invalid params");
        if (params) outcome = co_await call(parsed->method, *params, who);
        if ( ! parsed->has_id) co_return std::string{};
        object out;
        out.text("jsonrpc", "2.0");
        if (outcome) {
            out.raw("result", *outcome);
        } else {
            out.raw("error", object().number("code", outcome.error().code).text("message", outcome.error().message).done());
        }
        out.raw("id", parsed->id);
        co_return out.done();
    }

    ::asio::awaitable<std::string> answer(std::string line, client* who = nullptr) {
        auto const text = trimmed(line);
        if ( ! text.empty() && text.front() == '[') {
            auto const items = batch_of(text);
            if ( ! items) co_return co_await answer_one(text, who);
            std::vector<std::string> answers;
            for (auto const& item : *items) {
                auto one = co_await answer_one(item, who);
                if ( ! one.empty()) answers.push_back(std::move(one));
            }
            co_return array_of(answers);
        }
        co_return co_await answer_one(text, who);
    }

    // ------------------------------------------------------------------------
    // Clients

    struct client {
        std::shared_ptr<tcp::socket> socket;
        kth::select_channel<int> writing{1};
        std::map<std::string, std::string> scripts;   // scripthash or address -> last status
        std::optional<std::string> tip;               // last header sent, when subscribed
        std::map<std::string, std::string> transactions;  // txid -> last height
        std::map<std::string, std::string> proofs;        // txid -> last double-spend proof
        bool open = true;
        client() { (void)writing.try_send(0); }
    };

    static ::asio::awaitable<bool> write_line(client& who, std::string text) {
        auto executor = co_await ::asio::this_coro::executor;
        auto completion = co_await ::asio::experimental::make_select(::asio::experimental::receive(who.writing))
            .async_wait(executor, {}, std::stop_token{}, ::asio::as_tuple(::asio::deferred));
        auto& [locked, selected] = completion;
        (void)selected;
        if (locked) co_return false;
        text += '\n';
        auto [ec, written] = co_await ::asio::async_write(*who.socket, ::asio::buffer(text), ::asio::as_tuple(::asio::use_awaitable));
        (void)written;
        (void)who.writing.try_send(0);
        co_return ! ec;
    }

    // What a client follows is sent when the wallet support reports a change
    // (a new tip, a reorganization, a mempool transaction in or out, a
    // double-spend proof), not found by asking again on a timer; and only
    // what that change touched is read again.
    ::asio::awaitable<void> notify(std::shared_ptr<client> who) {
        uint64_t seen = co_await wallet.changed(0, std::chrono::seconds(0));
        while (who->open && ! stopping) {
            auto const asked = std::chrono::steady_clock::now();
            auto const now_seen = co_await wallet.changed(seen, notify_wait);
            if ( ! who->open || stopping) break;
            if (now_seen == seen) {
                // Nothing changed for a while; or, answered at once, the
                // wallet support stopped.
                if (std::chrono::steady_clock::now() - asked < notify_wait) break;
                continue;
            }
            auto const change = co_await wallet.touched(seen);
            seen = change.count;
            auto const touched = [&change](hash_digest const& key) {
                return ! change.keys || std::ranges::binary_search(*change.keys, key);
            };
            for (auto& [txid_text, last] : std::map<std::string, std::string>(who->transactions)) {
                auto const txid = decode_hash(txid_text);
                if ( ! txid || ! touched(*txid)) continue;
                auto const found = co_await wallet.transaction(*txid, std::nullopt);
                auto const height = found ? std::to_string(found->confirmed ? found->confirmed->height : 0) : std::string("null");
                auto const known = who->transactions.find(txid_text);
                if (known == who->transactions.end() || known->second == height) continue;
                known->second = height;
                if ( ! co_await write_line(*who, object().text("jsonrpc", "2.0").text("method", "blockchain.transaction.subscribe")
                        .raw("params", array_of({json_text(txid_text), height})).done())) {
                    who->open = false;
                    break;
                }
            }
            for (auto& [txid_text, last] : std::map<std::string, std::string>(who->proofs)) {
                auto const txid = decode_hash(txid_text);
                if ( ! txid || ! touched(*txid)) continue;
                auto const proof = co_await wallet.dsproof(*txid);
                auto const shown = proof ? dsproof_json(*proof) : std::string("null");
                auto const known = who->proofs.find(txid_text);
                if (known == who->proofs.end() || known->second == shown) continue;
                known->second = shown;
                if ( ! co_await write_line(*who, object().text("jsonrpc", "2.0").text("method", "blockchain.transaction.dsproof.subscribe")
                        .raw("params", array_of({json_text(txid_text), shown})).done())) {
                    who->open = false;
                    break;
                }
            }
            // A mempool change does not move the tip.
            if (who->tip && ! change.keys) {
                auto const now = co_await tip();
                if (now) {
                    auto const latest = header_json(now->first, now->second);
                    if (latest != *who->tip) {
                        who->tip = latest;
                        if ( ! co_await write_line(*who, object().text("jsonrpc", "2.0").text("method", "blockchain.headers.subscribe")
                                .raw("params", array_of({latest})).done())) break;
                    }
                }
            }
            // Keys copied first: a request may add or drop one while this waits.
            std::vector<std::string> keys;
            for (auto const& [key, last] : who->scripts) keys.push_back(key);
            for (auto const& key : keys) {
                auto const address = key.size() != 64;
                std::vector<param> params(1);
                params[0].what = param::kind::string;
                params[0].text = key;
                auto const watched = target_of(address ? "blockchain.address.subscribe" : "blockchain.scripthash.subscribe", params);
                if ( ! watched || ! touched(watched->key())) continue;
                auto const history = co_await wallet.history(*watched);
                if ( ! history) continue;
                auto const status = status_of(*history);
                auto const known = who->scripts.find(key);
                if (known == who->scripts.end() || known->second == status) continue;
                known->second = status;
                auto const name = address ? "blockchain.address.subscribe" : "blockchain.scripthash.subscribe";
                if ( ! co_await write_line(*who, object().text("jsonrpc", "2.0").text("method", name)
                        .raw("params", array_of({json_text(key), status})).done())) {
                    who->open = false;
                    break;
                }
            }
        }
    }

    ::asio::awaitable<void> serve(std::shared_ptr<tcp::socket> socket) {
        auto who = std::make_shared<client>();
        who->socket = socket;
        ::asio::co_spawn(acceptor.get_executor(), [self = shared_from_this(), who]() -> ::asio::awaitable<void> {
            co_await self->notify(who);
        }, ::asio::detached);
        std::string buffer;
        while ( ! stopping) {
            auto [ec, size] = co_await ::asio::async_read_until(*socket, ::asio::dynamic_buffer(buffer, max_line), '\n',
                ::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            auto const line = buffer.substr(0, size);
            buffer.erase(0, size);
            auto const reply = co_await answer(line, who.get());
            if ( ! reply.empty() && ! co_await write_line(*who, reply)) break;
        }
        who->open = false;
        std::error_code ignored;
        socket->close(ignored);
        std::lock_guard<std::mutex> guard(sockets_mutex);
        sockets.remove(socket);
    }

    ::asio::awaitable<void> run() {
        while ( ! stopping) {
            auto socket = std::make_shared<tcp::socket>(acceptor.get_executor());
            auto [ec] = co_await acceptor.async_accept(*socket, ::asio::as_tuple(::asio::use_awaitable));
            if (ec) break;
            {
                std::lock_guard<std::mutex> guard(sockets_mutex);
                sockets.push_back(socket);
            }
            ::asio::co_spawn(acceptor.get_executor(), [self = shared_from_this(), socket]() -> ::asio::awaitable<void> {
                co_await self->serve(socket);
            }, ::asio::detached);
        }
    }

    void close_all() {
        std::error_code ignored;
        acceptor.close(ignored);
        std::lock_guard<std::mutex> guard(sockets_mutex);
        for (auto const& socket : sockets) socket->close(ignored);
    }
};

std::expected<electrum_bind, std::string> parse_electrum_bind(std::string const& bind) {
    auto const colon = bind.rfind(':');
    if (colon == std::string::npos || colon + 1 == bind.size()) return std::unexpected("it is not ADDRESS:PORT: " + bind);
    auto address = bind.substr(0, colon);
    auto const port_text = bind.substr(colon + 1);
    if (address.size() >= 2 && address.front() == '[' && address.back() == ']') address = address.substr(1, address.size() - 2);
    uint32_t port = 0;
    auto const [end, parsed] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed != std::errc{} || end != port_text.data() + port_text.size() || port > 65535) {
        return std::unexpected("its port is not a number from 0 to 65535: " + bind);
    }
    std::error_code ec;
    auto const ip = ::asio::ip::make_address(address, ec);
    if (ec) return std::unexpected("its address is not an IP address: " + bind);
    if ( ! ip.is_loopback()) {
        return std::unexpected("its address is not a loopback address (127.0.0.0/8 or ::1): " + bind +
            ". The listener answers only wallets on this machine; a node must not learn what others' wallets watch.");
    }
    return electrum_bind{std::move(address), uint16_t(port)};
}

std::optional<std::string> electrum_bind_refusal(std::string const& bind) {
    auto const parsed = parse_electrum_bind(bind);
    if (parsed) return std::nullopt;
    return parsed.error();
}

electrum_server::electrum_server(service& wallet, domain::config::network network)
    : impl_(std::make_shared<impl>(wallet, network)) {}

electrum_server::~electrum_server() = default;

std::expected<uint16_t, std::error_code> electrum_server::listen(std::string const& address, uint16_t port) {
    std::error_code ec;
    auto const ip = ::asio::ip::make_address(address, ec);
    // Only this machine's wallets: no other address is ever bound.
    if (ec || ! ip.is_loopback()) return std::unexpected(error::listen_failed);
    tcp::endpoint const endpoint(ip, port);
    impl_->acceptor.open(endpoint.protocol(), ec);
    if ( ! ec) impl_->acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
    if ( ! ec) impl_->acceptor.bind(endpoint, ec);
    if ( ! ec) impl_->acceptor.listen(tcp::socket::max_listen_connections, ec);
    if (ec) return std::unexpected(error::listen_failed);
    auto const bound = impl_->acceptor.local_endpoint(ec);
    if (ec) return std::unexpected(error::listen_failed);
    return bound.port();
}

::asio::awaitable<void> electrum_server::run() {
    auto const self = impl_;
    co_await self->run();
}

void electrum_server::stop() {
    impl_->stopping = true;
    ::asio::post(impl_->acceptor.get_executor(), [state = impl_] { state->close_all(); });
}

::asio::awaitable<std::string> electrum_server::answer(std::string line) {
    auto const self = impl_;
    co_return co_await self->answer(std::move(line));
}

} // namespace kth::node::wallet
