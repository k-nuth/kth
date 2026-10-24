// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/detail/header_ingest.hpp>

#include <utility>

namespace kth::node::detail {

bool header_ingest::offer(network::peer_session::ptr const& peer,
                          domain::message::header::list headers) {
    // The total first: a refused batch from a peer with nothing queued must not
    // leave an empty entry behind.
    if (total_ >= total_capacity) {
        return false;
    }
    auto& queue = queues_[peer->nonce()];
    if (queue.batches.size() >= per_peer_capacity) {
        return false;
    }
    if (queue.batches.empty()) {
        ring_.push_back(peer->nonce());
    }
    queue.peer = peer;
    queue.batches.push_back(std::move(headers));
    ++total_;
    return true;
}

std::optional<ingested_headers> header_ingest::take() {
    if (ring_.empty()) {
        return std::nullopt;
    }
    auto const nonce = ring_.front();
    ring_.pop_front();

    auto it = queues_.find(nonce);
    if (it == queues_.end() || it->second.batches.empty()) {
        return std::nullopt;        // a peer is in the ring only with something queued
    }

    auto& queue = it->second;
    ingested_headers taken{queue.peer, std::move(queue.batches.front())};
    queue.batches.pop_front();
    --total_;

    if (queue.batches.empty()) {
        // Nothing left: the entry goes, and with it the session it was holding.
        queues_.erase(it);
    } else {
        // Back of the line, behind everyone who was waiting.
        ring_.push_back(nonce);
    }
    return taken;
}

size_t header_ingest::queued(uint64_t nonce) const {
    auto it = queues_.find(nonce);
    return it == queues_.end() ? 0 : it->second.batches.size();
}

header_ingest_channels::header_ingest_channels(::asio::any_io_executor const& executor)
    : commands(executor, command_capacity)
    , debt(executor, 1)
{}

bool header_ingest_channels::submit(network::peer_session::ptr const& peer,
                                    domain::message::header::list headers) {
    if (commands.try_send(std::error_code{}, ingest_submit{peer, std::move(headers)})) {
        return true;
    }
    owe();
    return false;
}

void header_ingest_channels::owe() {
    (void)debt.try_send(std::error_code{}, headers_owed{});
}

void header_ingest_channels::close() {
    commands.cancel();
    commands.close();
    debt.cancel();
    debt.close();
}

} // namespace kth::node::detail
