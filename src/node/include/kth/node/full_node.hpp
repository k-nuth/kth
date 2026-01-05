// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_FULL_NODE_HPP
#define KTH_NODE_FULL_NODE_HPP

// =============================================================================
// Full Node - Bitcoin Full Node Implementation
// =============================================================================
//
// This class represents a complete Bitcoin full node that combines:
// - P2P networking (via network::p2p_node composition) [non-WASM only]
// - Blockchain storage and validation (via blockchain::block_chain)
//
// ARCHITECTURE:
// -------------
// Uses composition instead of inheritance:
//   full_node HAS-A p2p_node (networking) [non-WASM only]
//   full_node HAS-A block_chain (blockchain)
//
// API:
// ----
// All lifecycle methods use C++20 coroutines (asio::awaitable).
// This is a breaking change from the callback-based API in v0.x.
//
// WASM (Emscripten):
// ------------------
// In WebAssembly builds, networking is disabled. Only blockchain
// operations are available.
//
// LEGACY FILES REPLACED:
// ----------------------
// This new design eliminates the need for:
// - Inheritance from network::p2p
// - Virtual method overrides (attach_*_session)
// - node::session_inbound, session_outbound, session_manual
//
// =============================================================================

#include <cstdint>
#include <memory>

#include <kth/blockchain.hpp>
#include <kth/infrastructure.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/node/configuration.hpp>
#include <kth/node/define.hpp>

#include <asio/awaitable.hpp>

namespace kth::node {

/// Startup mode selection
enum class start_modules {
    all,
    just_chain,
    just_p2p
};

// Helper to set cashaddr prefix based on network
struct multi_crypto_setter {
    multi_crypto_setter() {
        set_cashaddr_prefix("bitcoincash");
    }

#if ! defined(__EMSCRIPTEN__)
    explicit multi_crypto_setter(network::settings const& net_settings) {
#if defined(KTH_CURRENCY_BCH)
        switch (net_settings.identifier) {
            case netmagic::bch_mainnet:
                set_cashaddr_prefix("bitcoincash");
                break;
            case netmagic::bch_testnet:
            case netmagic::bch_testnet4:
            case netmagic::bch_scalenet:
                set_cashaddr_prefix("bchtest");
                break;
            case netmagic::bch_regtest:
                set_cashaddr_prefix("bchreg");
                break;
            default:
                set_cashaddr_prefix("");
        }
#endif
    }
#endif
};

/// A full node on the Bitcoin P2P network.
class KND_API full_node : public multi_crypto_setter {
public:
    using ptr = std::shared_ptr<full_node>;

    /// Construct the full node.
    explicit full_node(configuration const& configuration);

    /// Destructor - stops node if running.
    ~full_node();

    // Non-copyable, non-movable
    full_node(full_node const&) = delete;
    full_node& operator=(full_node const&) = delete;
    full_node(full_node&&) = delete;
    full_node& operator=(full_node&&) = delete;

    // Lifecycle
    // -------------------------------------------------------------------------

    /// Start the node (initialize chain and network).
    [[nodiscard]]
    ::asio::awaitable<code> start();

    /// Run the node (start P2P connections and protocol handlers).
    [[nodiscard]]
    ::asio::awaitable<code> run();

    /// Stop the node.
    void stop();

    /// Block until all work is complete.
    void join();

    /// Check if node is stopped.
    [[nodiscard]]
    bool stopped() const;

    // Properties
    // -------------------------------------------------------------------------

    /// Node configuration settings.
    [[nodiscard]]
    node::settings const& node_settings() const;

    /// Blockchain configuration settings.
    [[nodiscard]]
    blockchain::settings const& chain_settings() const;

#if ! defined(__EMSCRIPTEN__)
    /// Network configuration settings.
    [[nodiscard]]
    kth::network::settings const& network_settings() const;
#endif

    /// Blockchain query interface.
    [[nodiscard]]
    blockchain::block_chain& chain();

    /// Blockchain (for mining and advanced use).
    [[nodiscard]]
    blockchain::block_chain& chain_kth();

#if ! defined(__EMSCRIPTEN__)
    /// P2P network node.
    [[nodiscard]]
    kth::network::p2p_node& network();
#endif

    // Thread pools
    // -------------------------------------------------------------------------

    /// Thread pool for blockchain operations (validation, DB).
    [[nodiscard]]
    threadpool& chain_thread_pool();

#if ! defined(__EMSCRIPTEN__)
    /// Thread pool for network operations (P2P, sockets).
    [[nodiscard]]
    threadpool& network_thread_pool();
#endif

    // Subscriptions
    // -------------------------------------------------------------------------

    using block_channel_ptr = blockchain::block_chain::block_channel_ptr;
    using transaction_channel_ptr = blockchain::block_chain::transaction_channel_ptr;
    using ds_proof_channel_ptr = blockchain::block_chain::ds_proof_channel_ptr;

    /// Subscribe to blockchain reorganization events.
    [[nodiscard]]
    block_channel_ptr subscribe_blockchain();

    /// Subscribe to transaction pool acceptance events.
    [[nodiscard]]
    transaction_channel_ptr subscribe_transaction();

    /// Subscribe to DSProof pool acceptance events.
    [[nodiscard]]
    ds_proof_channel_ptr subscribe_ds_proof();

    /// Unsubscribe from blockchain events.
    void unsubscribe_blockchain(block_channel_ptr const& channel);

    /// Unsubscribe from transaction events.
    void unsubscribe_transaction(transaction_channel_ptr const& channel);

    /// Unsubscribe from DSProof events.
    void unsubscribe_ds_proof(ds_proof_channel_ptr const& channel);

    // Utilities
    // -------------------------------------------------------------------------

    /// Get the genesis block for a network.
    [[nodiscard]]
    static domain::chain::block get_genesis_block(domain::config::network network);

private:
    // Internal helpers
    bool handle_reorganized(code ec, size_t fork_height,
        block_const_ptr_list_const_ptr incoming,
        block_const_ptr_list_const_ptr outgoing);

    // Configuration references (stored in configuration object)
    node::settings const& node_settings_;
    blockchain::settings const& chain_settings_;
    domain::config::network network_type_;

    // Thread pool for blockchain operations (validation, DB)
    threadpool chain_thread_pool_;

    // Core components (composition, not inheritance)
#if ! defined(__EMSCRIPTEN__)
    kth::network::p2p_node network_;
#endif
    blockchain::block_chain chain_;
};

} // namespace kth::node

#endif // KTH_NODE_FULL_NODE_HPP
