// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/wallet/tokens.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <sstream>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::node::wallet {

namespace {

constexpr uint8_t op_return = 0x6a;
constexpr uint8_t op_pushdata1 = 0x4c;
constexpr uint8_t op_pushdata2 = 0x4d;
constexpr uint8_t op_pushdata4 = 0x4e;
// OP_RETURN, then 'BCMR' in a push of its own.
constexpr std::array<uint8_t, 6> bcmr_prefix{op_return, 0x04, 'B', 'C', 'M', 'R'};

// The data of the push at `at`, moving `at` past it; none at a non-push
// opcode or a push cut short.
std::optional<data_chunk> next_push(data_chunk const& script, size_t& at) {
    if (at >= script.size()) return std::nullopt;
    auto const opcode = script[at++];
    size_t size = 0;
    if (opcode < op_pushdata1) {
        size = opcode;
    } else if (opcode == op_pushdata1 || opcode == op_pushdata2 || opcode == op_pushdata4) {
        size_t const width = opcode == op_pushdata1 ? 1 : opcode == op_pushdata2 ? 2 : 4;
        if (script.size() - at < width) return std::nullopt;
        for (size_t i = 0; i < width; ++i) size |= size_t(script[at + i]) << (8 * i);
        at += width;
    } else {
        return std::nullopt;
    }
    if (script.size() - at < size) return std::nullopt;
    data_chunk out(script.begin() + std::ptrdiff_t(at), script.begin() + std::ptrdiff_t(at + size));
    at += size;
    return out;
}

std::optional<bcmr_publication> publication_of(data_chunk const& script, uint32_t index) {
    if (script.size() < bcmr_prefix.size() + 33 || ! std::equal(bcmr_prefix.begin(), bcmr_prefix.end(), script.begin())) {
        return std::nullopt;
    }
    size_t at = bcmr_prefix.size();
    // The hash in a push of 32 bytes.
    if (script[at] != 0x20) return std::nullopt;
    auto const hash = next_push(script, at);
    if ( ! hash || hash->size() != hash_size) return std::nullopt;
    bcmr_publication out;
    out.output = index;
    std::copy(hash->begin(), hash->end(), out.registry_hash.begin());
    while (at < script.size()) {
        auto const uri = next_push(script, at);
        if ( ! uri) return std::nullopt;
        out.uris.emplace_back(uri->begin(), uri->end());
    }
    return out;
}

bool reserves(domain::chain::token_data_t const& token) {
    if ( ! domain::chain::has_nft(token)) return false;
    auto const capability = domain::chain::get_nft(token).capability;
    return capability == domain::chain::capability_t::minting || capability == domain::chain::capability_t::mut;
}

} // namespace

bool is_data_carrier(data_chunk const& script) {
    return ! script.empty() && script.front() == op_return;
}

std::vector<bcmr_publication> bcmr_publications(domain::chain::transaction const& tx) {
    std::vector<bcmr_publication> out;
    auto const& outputs = tx.outputs();
    for (uint32_t index = 0; index < outputs.size(); ++index) {
        if (auto found = publication_of(outputs[index].script().bytes(), index)) out.push_back(std::move(*found));
    }
    return out;
}

bool registry_matches(byte_span registry, hash_digest const& published) {
    return sha256_hash(registry) == published;
}

std::expected<std::vector<hash_digest>, std::error_code> verify_authchain(hash_digest const& authbase,
        std::vector<domain::chain::transaction> const& chain) {
    if (chain.empty() || chain.front().hash() != authbase) return std::unexpected(error::bad_stream);
    std::vector<hash_digest> out{authbase};
    for (size_t at = 1; at < chain.size(); ++at) {
        auto const& previous = out.back();
        auto const& inputs = chain[at].inputs();
        auto const spends = std::ranges::any_of(inputs, [&](auto const& in) {
            return in.previous_output().hash() == previous && in.previous_output().index() == 0;
        });
        if ( ! spends) return std::unexpected(error::bad_stream);
        out.push_back(chain[at].hash());
    }
    return out;
}

token_supply supply_of(hash_digest const& category, std::vector<utxo> const& unspent, bool mempool) {
    token_supply out;
    for (auto const& coin : unspent) {
        if ( ! coin.token || coin.token->id != category) continue;
        if ( ! mempool && ! coin.height) continue;
        if (is_data_carrier(coin.script)) continue;
        auto const amount = uint64_t(std::max<int64_t>(0, domain::chain::get_amount(*coin.token)));
        out.total += amount;
        if (reserves(*coin.token)) out.reserved += amount;
    }
    out.circulating = out.total - out.reserved;
    return out;
}

std::vector<token_holding> holdings_of(std::vector<utxo> const& unspent) {
    std::map<hash_digest, token_holding> by_category;
    for (auto const& coin : unspent) {
        if ( ! coin.token) continue;
        auto& held = by_category[coin.token->id];
        held.category = coin.token->id;
        held.fungible += uint64_t(std::max<int64_t>(0, domain::chain::get_amount(*coin.token)));
        if (domain::chain::has_nft(*coin.token)) ++held.nfts;
    }
    std::vector<token_holding> out;
    for (auto& [category, held] : by_category) out.push_back(held);
    return out;
}

namespace detail {

// One line per step: authbase, txid, source, height, block, all hex.
identity_memory::identity_memory(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string base_text;
        std::string txid_text;
        unsigned from = 0;
        uint32_t height = 0;
        std::string block_text;
        if ( ! (fields >> base_text >> txid_text >> from >> height >> block_text)) continue;
        auto const base = decode_hash(base_text);
        auto const txid = decode_hash(txid_text);
        auto const block = decode_hash(block_text);
        if ( ! base || ! txid || ! block || from > unsigned(identity_step::source::registry)) continue;
        known_[*base].push_back(identity_step{*txid, identity_step::source(from), height, *block});
    }
}

std::optional<std::vector<identity_step>> identity_memory::find(hash_digest const& authbase) const {
    auto const found = known_.find(authbase);
    if (found == known_.end()) return std::nullopt;
    return found->second;
}

void identity_memory::keep(hash_digest const& authbase, std::vector<identity_step> chain) {
    // Only what the chain holds is kept: a mempool step is read again.
    std::erase_if(chain, [](identity_step const& step) { return step.from == identity_step::source::mempool; });
    if (chain.empty()) {
        known_.erase(authbase);
    } else {
        known_[authbase] = std::move(chain);
    }
    save();
}

void identity_memory::save() const {
    if (file_.empty()) return;
    auto const partial = std::filesystem::path(file_.string() + ".new");
    {
        std::ofstream out(partial, std::ios::trunc);
        for (auto const& [base, chain] : known_) {
            for (auto const& step : chain) {
                out << encode_hash(base) << ' ' << encode_hash(step.txid) << ' ' << unsigned(step.from) << ' '
                    << step.height << ' ' << encode_hash(step.block) << '\n';
            }
        }
        out.flush();
        if ( ! out) return;
    }
    std::error_code ignored;
    std::filesystem::rename(partial, file_, ignored);
}

} // namespace detail

} // namespace kth::node::wallet
