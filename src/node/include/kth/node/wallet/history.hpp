// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_WALLET_HISTORY_HPP
#define KTH_NODE_WALLET_HISTORY_HPP

#if ! defined(KTH_WITH_WALLET)
#error "Wallet support is an optional feature: build with the Conan options neutrino=True and wallet=True."
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/point.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/node/define.hpp>

namespace kth::node::wallet {

// Histories built from whole blocks. A watched target is a locking script,
// a script known by its SHA-256, or a token category. Its history is every
// output a block creates for it and every input that spends one of its
// earlier outputs, found by outpoint.

/// Which outputs a balance or a list of unspent outputs counts.
enum class token_filter : uint8_t {
    include,
    exclude,
    only,
};

[[nodiscard]]
KND_API
bool keeps(token_filter filter, domain::chain::token_data_opt const& token);

/// What a wallet watches.
struct KND_API target {
    enum class kind : uint8_t {
        /// A bare locking script (P2PKH, P2SH20, P2SH32, any script a wallet derives).
        script,
        /// A locking script known only by its SHA-256, its filter key (the
        /// Electrum script hash in internal byte order).
        script_hash,
        /// Every output that carries a token category.
        category,
    };

    kind what = kind::script;
    data_chunk bytes;

    [[nodiscard]] static target of_script(data_chunk script);
    [[nodiscard]] static target of_script_hash(hash_digest const& hash);
    [[nodiscard]] static target of_category(hash_digest const& category);

    /// Its key in the script filters.
    [[nodiscard]] hash_digest key() const;

    /// Whether an output belongs to it.
    [[nodiscard]] bool owns(domain::chain::output const& out) const;

    friend bool operator==(target const&, target const&) = default;
};

/// An output a target received.
struct received {
    uint32_t vout;
    uint64_t value;
    domain::chain::token_data_opt token;
    /// The locking script (a category's outputs go to many scripts).
    data_chunk script;
};

/// An input that spent one of a target's outputs.
struct spent {
    uint32_t vin;
    domain::chain::point prevout;
    uint64_t value;
    domain::chain::token_data_opt token;
};

/// A confirmed transaction of a target's history.
struct confirmed_tx {
    hash_digest txid;
    uint32_t height;
    hash_digest block;
    /// The transaction's position in its block.
    uint32_t position;
    std::vector<received> outputs;
    std::vector<spent> inputs;
};

/// An output a target owns, with where it was created.
struct coin {
    domain::chain::point outpoint;
    uint64_t value;
    domain::chain::token_data_opt token;
    data_chunk script;
    uint32_t height;
    /// The creating transaction's position in its block.
    uint32_t position;
};

/// An unspent output, confirmed or in the mempool.
struct utxo {
    domain::chain::point outpoint;
    uint64_t value;
    domain::chain::token_data_opt token;
    data_chunk script;
    /// None while in the mempool.
    std::optional<uint32_t> height;
};

/// The confirmed state of one target.
class KND_API target_state {
public:
    /// Watched from `start` on: blocks below it are never applied.
    target_state(target watched, uint32_t start);

    [[nodiscard]] target const& watched() const { return target_; }
    [[nodiscard]] hash_digest const& key() const { return key_; }
    [[nodiscard]] uint32_t start() const { return start_; }

    /// Every block from start() up to this height has been applied.
    [[nodiscard]] std::optional<uint32_t> synced() const { return synced_; }

    /// The first height not applied yet.
    [[nodiscard]] uint32_t next_height() const;

    /// Marks the blocks up to `height` applied (those without matches need no download).
    void set_synced(uint32_t height);

    /// Applies the block at `height`: its outputs first, then its inputs, since
    /// under canonical ordering a transaction may spend an output created
    /// later in the same block. Returns whether the history changed.
    bool apply_block(uint32_t height, hash_digest const& block, domain::chain::transaction::list const& txs);

    /// Forgets every block above `fork`. Returns whether the history changed.
    bool rollback(uint32_t fork);

    /// In chain order.
    [[nodiscard]] std::vector<confirmed_tx> const& history() const { return history_; }

    /// The outputs it received, spent or not.
    [[nodiscard]] std::map<domain::chain::point, coin> const& coins() const { return coins_; }

    /// Whether a confirmed transaction spent the output.
    [[nodiscard]] bool is_spent(domain::chain::point const& outpoint) const;

    /// The confirmed outputs no confirmed transaction spent, in chain order
    /// (height, position in the block, index), as Fulcrum lists them.
    [[nodiscard]] std::vector<coin> unspent() const;

private:
    target target_;
    hash_digest key_;
    uint32_t start_;
    std::optional<uint32_t> synced_;
    std::vector<confirmed_tx> history_;
    std::map<domain::chain::point, coin> coins_;
    /// The transaction, and its height, that spent each spent coin.
    std::map<domain::chain::point, std::pair<hash_digest, uint32_t>> spent_by_;
};

/// A transaction of the mempool copy.
struct mempool_tx {
    domain::chain::transaction tx;
    hash_digest txid;
    size_t size;
    /// Known when the values of all the outputs it spends are known.
    std::optional<uint64_t> fee;
    /// The order it arrived in.
    uint64_t sequence;
};

/// The wallet's own copy of the mempool, held to a number of bytes: on a full
/// node, the transactions of the node's mempool. A fee is known when the
/// values of all the outputs its transaction spends are.
class KND_API mempool_copy {
public:
    /// The value of a confirmed output, when the client knows it.
    using value_lookup = std::function<std::optional<uint64_t>(domain::chain::point const&)>;

    explicit mempool_copy(size_t max_bytes);

    [[nodiscard]] bool contains(hash_digest const& txid) const;
    [[nodiscard]] mempool_tx const* find(hash_digest const& txid) const;
    [[nodiscard]] std::map<hash_digest, mempool_tx> const& transactions() const { return txs_; }
    [[nodiscard]] size_t size() const { return txs_.size(); }
    [[nodiscard]] size_t bytes() const { return bytes_; }

    /// The mempool transaction that spends an outpoint.
    [[nodiscard]] std::optional<hash_digest> spender(domain::chain::point const& outpoint) const;

    /// Whether the transaction spends an output of a mempool transaction.
    [[nodiscard]] bool has_unconfirmed_parent(domain::chain::transaction const& tx) const;

    /// Adds a transaction (never a coinbase). Returns false when it is there
    /// already. The oldest transactions leave when the copy is full.
    bool insert(domain::chain::transaction tx, value_lookup const& known);

    /// Removes what a block confirms, what conflicts with it and the
    /// descendants of what conflicts. Returns the removed hashes.
    std::vector<hash_digest> remove_for_block(domain::chain::transaction::list const& txs);

    /// Removes one transaction (it left the mempool it copies). Returns
    /// whether it was there.
    bool erase(hash_digest const& txid);

    void clear();

private:
    void remove(hash_digest const& txid);

    size_t max_bytes_;
    size_t bytes_ = 0;
    uint64_t sequence_ = 0;
    std::map<hash_digest, mempool_tx> txs_;
    std::map<domain::chain::point, hash_digest> spends_;
    std::map<uint64_t, hash_digest> order_;
};

} // namespace kth::node::wallet

#endif // KTH_NODE_WALLET_HISTORY_HPP
