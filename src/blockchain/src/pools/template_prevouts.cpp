// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/template_prevouts.hpp>

#include <cstring>
#include <memory>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <asio/cancellation_state.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/blockchain/populate/populate_base.hpp>

namespace kth::blockchain {
namespace {

// Only prevout resolution is exposed. The duplicate/pooled population methods
// write global transaction-validation scratch and are deliberately unused.
class confirmed_prevout_reader final : private populate_base {
public:
    explicit confirmed_prevout_reader(block_chain const& chain)
        : populate_base(chain.executor(), 1, chain) {}

    code populate(size_t tip, domain::chain::output_point const& outpoint, uint32_t mtp) const {
        return populate_prevout(tip, outpoint, true, mtp);
    }
};

struct digest_hasher {
    size_t operator()(hash_digest const& hash) const noexcept {
        size_t value;
        std::memcpy(&value, hash.data(), sizeof(value));
        return value;
    }
};

// Arguments live in the coroutine frame by value. In particular, a cancelled
// outer request never leaves a worker borrowing its vector or publication.
awaitable_expected<std::vector<mempool_entry>> refresh_on_chain_executor(
    block_chain const& chain, std::vector<mempool_entry> entries,
    block_chain::chain_view_ptr published) {
    auto const cancellation = co_await ::asio::this_coro::cancellation_state;
    auto cancelled = [&]() {
        return chain.stopped() || cancellation.cancelled() != ::asio::cancellation_type::none;
    };
    if (cancelled()) {
        co_return std::unexpected(error::service_stopped);
    }
    if (!published || !published->state || published->state->height() == 0 ||
        published->state->height() - 1 != published->connected_tip_height ||
        published->connected_tip_height > std::numeric_limits<uint32_t>::max()) {
        co_return std::unexpected(error::operation_failed);
    }

    auto const count = entries.size();
    std::unordered_map<hash_digest, size_t, digest_hasher> index;
    index.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].tx || !index.emplace(entries[i].tx->hash(), i).second) {
            co_return std::unexpected(error::operation_failed);
        }
    }

    // The work queue has exactly count slots and each transaction enters it at
    // most once. It owns the complete refusal debt, including descendants that
    // were already visited. No dropped parent can later look confirmed merely
    // because it was removed from the view passed to the selector.
    std::vector<std::vector<size_t>> children(count);
    for (size_t i = 0; i < count; ++i) {
        for (auto const& in : entries[i].tx->inputs()) {
            if (auto const parent = index.find(in.previous_output().hash()); parent != index.end()) {
                children[parent->second].push_back(i);
            }
        }
    }
    std::vector<uint8_t> excluded(count, 0);
    std::vector<size_t> debt(count);
    size_t tail = 0;
    auto exclude = [&](size_t first) {
        if (!excluded[first]) {
            excluded[first] = 1;
            debt[tail++] = first;
        }
    };

    confirmed_prevout_reader reader(chain);
    auto const candidate_height = published->state->height();
    auto const mtp = published->state->median_time_past();
    for (size_t i = 0; i < count; ++i) {
        if (cancelled()) {
            co_return std::unexpected(error::service_stopped);
        }
        auto private_tx = std::make_shared<domain::message::transaction>(*entries[i].tx);
        entries[i].tx = private_tx;
        for (auto const& in : private_tx->inputs()) {
            if (cancelled()) {
                co_return std::unexpected(error::service_stopped);
            }
            auto const& outpoint = in.previous_output();
            outpoint.validation = {};
            if (outpoint.is_null()) {
                exclude(i); // a coinbase cannot be a mempool candidate
                break;
            }
            if (auto const parent = index.find(outpoint.hash()); parent != index.end()) {
                auto const& outputs = entries[parent->second].tx->outputs();
                if (outpoint.index() >= outputs.size()) {
                    exclude(i);
                    break;
                }
                auto& metadata = outpoint.validation;
                metadata.cache = outputs[outpoint.index()];
                metadata.height = candidate_height;
                metadata.median_time_past = mtp;
                metadata.from_mempool = true;
                continue;
            }

            if (auto const ec = reader.populate(published->connected_tip_height, outpoint, mtp)) {
                co_return std::unexpected(ec);
            }
            if (!outpoint.validation.cache.is_valid()) {
                exclude(i);
                break;
            }
        }
    }

    // Propagate every refused input through the snapshot's original graph.
    // The bounded queue grows only on the first visit to an entry.
    for (size_t head = 0; head < tail; ++head) {
        if (cancelled()) {
            co_return std::unexpected(error::service_stopped);
        }
        for (auto const child : children[debt[head]]) {
            exclude(child);
        }
    }

    std::vector<mempool_entry> retained;
    retained.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (!excluded[i]) retained.push_back(std::move(entries[i]));
    }
    co_return retained;
}
} // namespace

awaitable_expected<std::vector<mempool_entry>> refresh_template_prevouts(
    block_chain const& chain, std::vector<mempool_entry> entries,
    block_chain::chain_view_ptr published) {
    auto const cancellation = co_await ::asio::this_coro::cancellation_state;
    if (chain.stopped() || cancellation.cancelled() != ::asio::cancellation_type::none) {
        co_return std::unexpected(error::service_stopped);
    }
    try {
        co_return co_await ::asio::co_spawn(chain.executor(),
            refresh_on_chain_executor(chain, std::move(entries), std::move(published)),
            ::asio::use_awaitable);
    } catch (std::system_error const& e) {
        if (e.code() == ::asio::error::operation_aborted) {
            co_return std::unexpected(error::service_stopped);
        }
        throw;
    }
}
} // namespace kth::blockchain
