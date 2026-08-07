// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TEST_REORG_CHAIN_FIXTURE_HPP
#define KTH_BLOCKCHAIN_TEST_REORG_CHAIN_FIXTURE_HPP

#include <filesystem>
#include <string>
#include <unistd.h>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/database.hpp>
#include <kth/database/settings.hpp>
#include <kth/domain/multi_crypto_support.hpp>

namespace kth::test {

// A real block_chain on a throwaway directory.
//
// The reorg path (disconnect -> switch -> re-download) spans the header index,
// the flat-file block store, the undo files and UTXO-Z, and its bugs live in how
// those agree with each other. Testing it against mocks would mostly re-assert
// the mocks, so this stands up the actual stack instead — the same create/open
// sequence the node performs at startup.
//
// Regtest is used deliberately: its proof-of-work target is trivial, so a test
// can build competing chains without mining.
struct chain_fixture {
    // The freshness half of synchronization compares the connected tip's
    // timestamp against notify_limit_hours. A test that needs a chain which is
    // complete but stale cannot move the clock or the blocks — the connect path
    // refuses a chain old enough to look like initial sync — so it moves the
    // limit instead.
    chain_fixture(char const* tag, uint32_t notify_limit_hours)
        : chain_fixture(tag)
    {
        chain_settings_.notify_limit_hours = notify_limit_hours;
    }

    explicit chain_fixture(char const* tag)
        : dir_(make_dir(tag))
        , chain_settings_(domain::config::network::regtest)
    {
        db_settings_.directory = dir_;

        // Same two steps as executor::init_directory followed by block_chain::start.
        database::data_base db(db_settings_);
        auto const genesis = genesis_block();
        created_ = db.create(genesis);
    }

    ~chain_fixture() {
        // Destroy in dependency order and let the destructors do the closing:
        // block_chain::~block_chain already closes (which stops), so calling
        // close() here would close twice — and removing the directory first
        // would run that second close against a datadir that no longer exists.
        organizer_.reset();
        chain_.reset();

        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    chain_fixture(chain_fixture const&) = delete;
    chain_fixture& operator=(chain_fixture const&) = delete;

    [[nodiscard]] bool created() const { return created_; }

    // Construct and start the chain. Separate from the constructor so a test can
    // assert on creation failure without the chain half-built.
    [[nodiscard]] bool start() {
        if ( ! created_) return false;
        chain_ = std::make_unique<blockchain::block_chain>(
            chain_settings_, db_settings_, domain::config::network::regtest,
            /*relay_transactions*/ false);
        if ( ! chain_->start(kth::get_disk_magic(domain::config::network::regtest))) {
            return false;
        }

        // The node performs this second step at startup and the rest of the stack
        // depends on it: block_chain::start loads headers into the index, but the
        // ACTIVE CHAIN (height -> index) is materialized by the organizer. Until
        // sync_tip() runs, active_at() answers null_index for every height, so
        // fetch_blocks_raw and disconnect_block would find nothing.
        organizer_ = std::make_unique<blockchain::header_organizer>(
            chain_->headers(), chain_settings_, domain::config::network::regtest);
        if ( ! organizer_->start()) {
            return false;
        }
        organizer_->sync_tip();

        // Same as full_node::run_sync: the organizer learns how far blocks are
        // validated at startup, since nothing else tells it until a new block is
        // stored and deep-reorg parking measures against that height.
        if (auto const heights = chain_->get_last_heights(); heights) {
            organizer_->note_block_validated(static_cast<int32_t>(heights->block));
        }
        return true;
    }

    // Close the chain and bring it back up on the same directory, which is what a
    // process restart does: the header index is rebuilt from the persisted
    // by-height headers, and the active chain is materialized again by sync_tip().
    // Nothing in memory survives, so whatever the node comes back on is whatever
    // was written to disk.
    [[nodiscard]] bool restart() {
        organizer_.reset();
        chain_.reset();
        return start();
    }

    // Bring the chain down and leave it down, so the directory can be opened by
    // something else — a second block_chain with different settings reading the
    // data this one wrote. Two live opens of one data directory is not something
    // the stack supports: the LMDB environment, UTXO-Z and the flat-file stores
    // each hold their own handles and cached state for that path.
    //
    // `chain()` and `organizer()` must not be used after this.
    void close() {
        organizer_.reset();
        chain_.reset();
    }

    [[nodiscard]] blockchain::header_organizer& organizer() { return *organizer_; }

    [[nodiscard]] blockchain::block_chain& chain() { return *chain_; }
    [[nodiscard]] std::filesystem::path const& dir() const { return dir_; }

private:
    static std::filesystem::path make_dir(char const* tag) {
        auto const p = std::filesystem::temp_directory_path() /
            ("kth_reorg_e2e_" + std::string(tag) + "_" + std::to_string(getpid()));
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        std::filesystem::create_directories(p, ec);
        return p;
    }

    static domain::chain::block genesis_block() {
        // The regtest genesis, which the store is seeded with before any test
        // chain is appended to it.
        return domain::chain::block::genesis_regtest();
    }

    std::filesystem::path dir_;
    blockchain::settings chain_settings_;
    database::settings db_settings_;
    bool created_{false};
    std::unique_ptr<blockchain::block_chain> chain_;
    std::unique_ptr<blockchain::header_organizer> organizer_;
};

} // namespace kth::test

#endif // KTH_BLOCKCHAIN_TEST_REORG_CHAIN_FIXTURE_HPP
