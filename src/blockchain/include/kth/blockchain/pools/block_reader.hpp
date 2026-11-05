// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_BLOCK_READER_HPP
#define KTH_BLOCKCHAIN_POOLS_BLOCK_READER_HPP

#include <filesystem>
#include <memory>
#include <utility>

#include <asio/thread_pool.hpp>
#include <kth/blockchain/define.hpp>
#include <kth/database/block_store.hpp>
#include <kth/infrastructure/expect.hpp>
#include <kth/infrastructure/utility/asio_helper.hpp>

namespace kth::blockchain {

// One disk owner with bounded request admission and an LRU of immutable blocks.
// The portable stdio backend runs only on its dedicated executor. Callers
// suspend through channels; neither the queue nor the cache borrows chain state.
// Cancellation withdraws a reply and skips queued work. An already-started
// portable file read finishes on the disk thread; destruction joins it rather
// than abandoning work or destroying its storage underneath it.
struct KB_API block_reader {
    using result = expect<std::pair<block_const_ptr, size_t>>;
    struct limits {
        size_t requests = 32;
        size_t cache_entries = 8;
        size_t cache_bytes = 64 * 1024 * 1024; // serialized bytes; parsed objects are also entry-bounded
    };

    block_reader(std::filesystem::path path, database::block_store::magic_t magic, limits budget);
    ~block_reader();
    block_reader(block_reader const&) = delete;
    block_reader& operator=(block_reader const&) = delete;

    // Non-coroutine wrapper: captures an owning endpoint before returning a
    // lazy awaitable, so even an operation not yet started survives stop/close.
    [[nodiscard]] ::asio::awaitable<result>
    fetch_block(hash_digest hash, size_t height, database::flat_file_pos pos) const;
    // Lifecycle calls are serialized by the owning chain. Requests may already
    // be suspended at start; stop closes both admission and reply waits.
    void start();
    void stop();

private:
    struct state;
    static ::asio::awaitable<void> run(std::shared_ptr<state> owned);
    static ::asio::awaitable<result> fetch_owned(std::shared_ptr<state> owned,
        hash_digest hash, size_t height, database::flat_file_pos pos);
    std::shared_ptr<state> state_;
    ::asio::thread_pool worker_{1};
    bool started_ = false; // lifecycle owner only; never read by the disk task
};

} // namespace kth::blockchain
#endif
