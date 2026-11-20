// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_TOKENS_HPP
#define KTH_NODE_WALLET_TOKENS_HPP

// CashTokens and BCMR in wallet support, with no index: a category's state
// is rebuilt from the blocks its category key matches, and an identity's
// authchain is followed one spend of its identity output at a time. The node
// never fetches a registry: an app downloads it from the published URIs and
// checks the bytes with registry_matches().

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/token_data.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/define.hpp>
#include <kth/node/wallet/history.hpp>

namespace kth::node::wallet {

/// A token category's fungible supply, as the CashTokens specification
/// defines it: the total is the amount in every unspent output of the
/// category (none is provably unspendable: consensus puts no token in a
/// data-carrier output), the reserved part sits in outputs that also hold a
/// minting or mutable NFT, and the rest circulates.
struct token_supply {
    uint64_t total = 0;
    uint64_t reserved = 0;
    uint64_t circulating = 0;
};

/// What a target holds of one category.
struct token_holding {
    hash_digest category{};
    uint64_t fungible = 0;
    size_t nfts = 0;
};

/// A token category, rebuilt from its outputs.
struct category_state {
    hash_digest category{};
    /// The genesis transaction: the one that spends output 0 of the
    /// transaction whose hash is the category id. None when the source does
    /// not read back to it (a birthday after it).
    std::optional<hash_digest> genesis;
    std::optional<uint32_t> genesis_height;
    /// Every unspent output of the category, confirmed first, then those in
    /// the mempool (no height).
    std::vector<utxo> unspent;
    /// From the confirmed outputs, and from all of them.
    token_supply supply;
    token_supply mempool_supply;
    /// Locking scripts holding the category, and its NFTs.
    size_t holders = 0;
    size_t nfts = 0;
};

/// A BCMR publication output: OP_RETURN <'BCMR'> <hash> [<uri> ...].
struct bcmr_publication {
    uint32_t output = 0;
    /// SHA-256 of the registry, in OP_SHA256 byte order (as pushed).
    hash_digest registry_hash{};
    /// UTF-8, as pushed: HTTPS without a scheme, or with one (ipfs://...).
    std::vector<std::string> uris;
};

/// One transaction of an authchain.
struct identity_step {
    enum class source : uint8_t {
        /// Read from the chain: height and block are known.
        chain,
        /// In the mempool.
        mempool,
        /// From a registry's authchain extension, checked link by link but
        /// not looked up.
        registry,
    };
    hash_digest txid{};
    source from = source::chain;
    uint32_t height = 0;
    hash_digest block{};
};

/// An identity (BCMR): its authchain from the authbase to the authhead,
/// whose identity output (output 0) is unspent, or a data-carrier output
/// when the identity is burned.
struct identity {
    hash_digest authbase{};
    std::vector<identity_step> chain;
    bool burned = false;
    /// The authhead's first publication output: the definitive one.
    std::optional<bcmr_publication> publication;
    /// Its later publication outputs, which the specification ignores.
    std::vector<bcmr_publication> ignored;
};

/// The publication outputs of a transaction, in output order.
KND_API std::vector<bcmr_publication> bcmr_publications(domain::chain::transaction const& tx);

/// Whether downloaded registry bytes are the ones a publication output
/// committed to.
KND_API bool registry_matches(byte_span registry, hash_digest const& published);

/// A registry's authchain extension, raw transactions from the authbase on
/// (the specification's first two checks): the first hashes to `authbase`
/// and each one spends output 0 of the one before. Their hashes, or
/// bad_stream: the registry's view of the chain diverged from it.
KND_API std::expected<std::vector<hash_digest>, std::error_code> verify_authchain(hash_digest const& authbase,
    std::vector<domain::chain::transaction> const& chain);

/// The supply of `category` in `unspent`; with `mempool` false, only the
/// confirmed outputs count.
KND_API token_supply supply_of(hash_digest const& category, std::vector<utxo> const& unspent, bool mempool);

/// What `unspent` holds, per category, in category order.
KND_API std::vector<token_holding> holdings_of(std::vector<utxo> const& unspent);

/// Whether a locking script is a data-carrier output's (OP_RETURN).
KND_API bool is_data_carrier(data_chunk const& script);

namespace detail {

// The identities a wallet followed: each authbase's chain as last read.
// The wallet's own data (it grows with the identities it follows, not with
// the chain), kept in a file when the source has a folder.
class KND_API identity_memory {
public:
    identity_memory() = default;
    explicit identity_memory(std::filesystem::path file);

    [[nodiscard]] std::optional<std::vector<identity_step>> find(hash_digest const& authbase) const;
    void keep(hash_digest const& authbase, std::vector<identity_step> chain);

private:
    void save() const;

    std::filesystem::path file_;
    std::map<hash_digest, std::vector<identity_step>> known_;
};

} // namespace detail

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_TOKENS_HPP
