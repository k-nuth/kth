// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Wallet support answered from peers: headers, filter headers and filters.
// Headers go through the header organizer, as a kth node validates them.
// Every peer that serves a filter type gives its checkpoints, as many as the
// chain has thousands of blocks; lists that differ are settled by the block
// where their filter headers part, and the peer whose filter does not fit it
// is refused. The first peer asked moves on every round, and above the last
// checkpoint another peer's filter headers are compared with the stored ones.

#include "peer_source.hpp"

#include <algorithm>
#include <array>

#include <spdlog/spdlog.h>

#include <kth/node/wallet/blocks.hpp>

namespace kth::node::wallet::detail {

using network::raw_message;

// ----------------------------------------------------------------------------
// The headers file: the active chain's 80-byte headers, replayed through the
// organizer on open, so a damaged or foreign file is refused.

void peer_impl::restore() {
    std::ifstream input(header_file, std::ios::binary | std::ios::ate);
    if ( ! input) return;
    auto const length = uint64_t(std::streamoff(input.tellg()));
    if (length < header_size || length % header_size != 0 || length / header_size > index.capacity()) return;
    auto const count = length / header_size;
    input.seekg(0);
    std::array<uint8_t, header_size> bytes{};
    auto read_header = [&]() -> std::optional<message::header> {
        if ( ! input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return std::nullopt;
        byte_reader reader(bytes);
        auto header = domain::chain::header::from_data(reader);
        if ( ! header) return std::nullopt;
        return message::header(*header);
    };
    auto const first = read_header();
    if ( ! first || domain::chain::hash(*first) != index.get_hash(index.active_at(0))) return;
    for (uint64_t height = 1; height < count;) {
        message::header::list batch;
        auto const end = std::min<uint64_t>(count, height + headers_per_answer);
        for (; height < end; ++height) {
            auto header = read_header();
            if ( ! header) return;
            batch.push_back(std::move(*header));
        }
        auto const result = organizer.add_headers(batch);
        if (result.reorg_candidate) organizer.adopt_tip(result.reorg_branch_head);
        if (result.error && result.error != error::stale_chain) return;
    }
    written_height = organizer.header_height();
    written_tip = index.get_hash(index.active_at(written_height));
}

void peer_impl::persist() {
    if (header_file.empty()) return;
    auto const height = organizer.header_height();
    auto const append = written_height >= 0 && written_height <= height &&
        index.get_hash(index.active_at(written_height)) == written_tip;
    if (append && written_height == height) return;
    std::ofstream output(header_file, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if ( ! output) return;
    for (int32_t at = append ? written_height + 1 : 0; at <= height; ++at) {
        std::array<uint8_t, header_size> bytes{};
        byte_writer writer(bytes);
        if ( ! index.get_header(index.active_at(at)).to_data(writer, true)) return;
        output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    }
    output.flush();
    if ( ! output) return;
    written_height = height;
    written_tip = index.get_hash(index.active_at(height));
}

uint32_t peer_impl::header_tip() const {
    return uint32_t(std::max(0, organizer.header_height()));
}

std::optional<hash_digest> peer_impl::hash_at(uint32_t height) const {
    if (height > header_tip()) return std::nullopt;
    auto const at = index.active_at(int32_t(height));
    if (at == blockchain::header_index::null_index) return std::nullopt;
    return index.get_hash(at);
}

std::optional<domain::chain::header> peer_impl::header(uint32_t height) const {
    if (height > header_tip()) return std::nullopt;
    return index.get_header(index.active_at(int32_t(height)));
}

// ----------------------------------------------------------------------------
// Headers

::asio::awaitable<std::error_code> peer_impl::sync_headers(std::shared_ptr<peer_link> link) {
    while ( ! stopping) {
        auto const tip_before = index.get_hash(index.active_at(organizer.header_height()));
        message::get_headers ask(locator(index), null_hash);
        auto answer = co_await request(link, ask, [](raw_message const& m) {
            return m.heading.command() == message::headers::command ? step::done : step::skip;
        });
        if ( ! answer) co_return answer.error();
        byte_reader reader(answer->back().payload);
        auto const headers = message::headers::from_data(reader, link->session->negotiated_version());
        if ( ! headers || ! headers->is_sequential()) co_return error::bad_stream;
        auto const& elements = headers->elements();
        if (elements.empty()) co_return error::success;
        if (index.size() + elements.size() > index.capacity()) co_return error::operation_failed;
        auto const result = organizer.add_headers(elements);
        if (result.reorg_candidate) {
            organizer.adopt_tip(result.reorg_branch_head);
            on_reorg(uint32_t(std::max(0, result.reorg_fork_height)));
        } else if (result.error && result.error != error::stale_chain) {
            co_return error::bad_stream;
        }
        auto const tip_after = index.get_hash(index.active_at(organizer.header_height()));
        update_status();
        if (elements.size() < headers_per_answer || tip_after == tip_before) co_return error::success;
    }
    co_return error::service_stopped;
}

// The filters above the fork go with the blocks they were built from, and so
// does what the targets read from them. The mempool copy stays: what the new
// branch confirms leaves it as its blocks are read.
void peer_impl::on_reorg(uint32_t fork) {
    spdlog::info("[wallet] Chain reorganization above height {}", fork);
    ++generation;
    if (scripts) (void)scripts->truncate(fork + 1);
    if (prefixes) (void)prefixes->truncate(fork + 1);
    for (auto& [key, state] : targets) (void)state.rollback(fork);
    if (live_tip) live_tip = std::min(*live_tip, fork);
    std::erase_if(located, [fork](auto const& entry) { return entry.second.confirmed.height > fork; });
    std::erase_if(located_order, [this](hash_digest const& txid) { return ! located.contains(txid); });
    std::erase_if(served_by, [fork](auto const& range) { return std::get<2>(range) > fork; });
    synced = false;
    changes.bump();
}

// ----------------------------------------------------------------------------
// Filter headers and filters

::asio::awaitable<peer_impl::header_answer> peer_impl::filter_hashes(std::shared_ptr<peer_link> link, uint8_t type,
        uint32_t start, hash_digest const& stop) {
    message::get_compact_filter_headers ask{type, start, stop};
    auto const version = link->session->negotiated_version();
    auto answer = co_await request(link, ask, [type, stop, version](raw_message const& m) {
        if (m.heading.command() != message::compact_filter_headers::command) return step::skip;
        byte_reader reader(m.payload);
        auto const parsed = message::compact_filter_headers::from_data(reader, version);
        return parsed && parsed->filter_type == type && parsed->stop_hash == stop ? step::done : step::skip;
    });
    if ( ! answer) co_return std::unexpected(answer.error());
    byte_reader reader(answer->back().payload);
    auto parsed = message::compact_filter_headers::from_data(reader, version);
    if ( ! parsed) co_return std::unexpected(error::bad_stream);
    co_return std::move(*parsed);
}

// The filter header at `before` (the block before a birthday), chained
// from the agreed checkpoint at or below it.
::asio::awaitable<std::expected<hash_digest, std::error_code>> peer_impl::header_before(std::shared_ptr<peer_link> link,
        uint8_t type, uint32_t before, std::vector<hash_digest> const& checkpoints) {
    auto const base = (before / checkpoint_interval) * checkpoint_interval;
    hash_digest previous = null_hash;
    uint32_t start = 0;
    if (base > 0) {
        auto const at = base / checkpoint_interval - 1;
        if (at >= checkpoints.size()) co_return std::unexpected(error::not_found);
        previous = checkpoints[at];
        if (before == base) co_return previous;
        start = base + 1;
    }
    auto const stop = hash_at(before);
    if ( ! stop) co_return std::unexpected(error::not_found);
    auto answer = co_await filter_hashes(link, type, start, *stop);
    if ( ! answer) co_return std::unexpected(answer.error());
    if (answer->previous_filter_header != previous || answer->filter_hashes.size() != size_t(before - start + 1)) {
        co_return std::unexpected(error::bad_stream);
    }
    for (auto const& hash : answer->filter_hashes) previous = next_filter_header_from_hash(hash, previous);
    co_return previous;
}

::asio::awaitable<std::expected<data_chunk, std::error_code>> peer_impl::filter_of(std::shared_ptr<peer_link> link,
        uint8_t type, uint32_t height) {
    auto const stop = hash_at(height);
    if ( ! stop) co_return std::unexpected(error::not_found);
    message::get_compact_filters ask{type, height, *stop};
    auto const version = link->session->negotiated_version();
    auto const wanted = *stop;
    auto answer = co_await request(link, ask, [type, wanted, version](raw_message const& m) {
        if (m.heading.command() != message::compact_filter::command) return step::skip;
        byte_reader reader(m.payload);
        auto const parsed = message::compact_filter::from_data(reader, version);
        return parsed && parsed->filter_type == type && parsed->block_hash == wanted ? step::done : step::skip;
    });
    if ( ! answer) co_return std::unexpected(answer.error());
    byte_reader reader(answer->back().payload);
    auto parsed = message::compact_filter::from_data(reader, version);
    if ( ! parsed) co_return std::unexpected(error::bad_stream);
    co_return std::move(parsed->filter);
}

::asio::awaitable<std::optional<std::vector<bool>>> peer_impl::judge(uint8_t type, uint32_t height, std::vector<data_chunk> const& filters) {
    auto const blk = co_await fetch_block(height, false);
    if ( ! blk) co_return std::nullopt;
    std::vector<bool> fits;
    for (auto const& filter : filters) {
        if (type == script_type) {
            fits.push_back(bool(check_script_filter(**blk, filter)));
        } else {
            fits.push_back( ! check_input_prefix_filter(**blk, filter));
        }
    }
    co_return fits;
}

::asio::awaitable<void> peer_impl::settle(uint8_t type, uint32_t height, std::vector<std::shared_ptr<peer_link>> parties) {
    std::vector<data_chunk> filters;
    std::vector<std::shared_ptr<peer_link>> answered;
    for (auto const& party : parties) {
        auto filter = co_await filter_of(party, type, height);
        if ( ! filter) {
            refuse(*party, "no filter of type " + std::to_string(type) + " for block " + std::to_string(height) + " in a dispute");
            continue;
        }
        filters.push_back(std::move(*filter));
        answered.push_back(party);
    }
    if (answered.empty()) co_return;
    auto const fits = co_await judge(type, height, filters);
    if ( ! fits) co_return;
    auto const fitting = size_t(std::ranges::count(*fits, true));
    for (size_t at = 0; at < answered.size(); ++at) {
        if (fitting == answered.size() && answered.size() > 1) {
            refuse(*answered[at], "filters of block " + std::to_string(height) + " that differ from another peer's and fit it as well");
        } else if ( ! (*fits)[at]) {
            refuse(*answered[at], "a filter that does not fit block " + std::to_string(height));
        }
    }
}

::asio::awaitable<std::expected<std::vector<hash_digest>, std::error_code>> peer_impl::agreed_checkpoints(uint8_t type,
        hash_digest const& tip_hash, uint32_t tip) {
    auto const wanted = size_t(tip / checkpoint_interval);
    std::vector<std::pair<std::shared_ptr<peer_link>, std::vector<hash_digest>>> lists;
    for (auto const& link : filter_servers(type)) {
        message::get_compact_filter_checkpoint ask{type, tip_hash};
        auto const version = link->session->negotiated_version();
        auto answer = co_await request(link, ask, [type, tip_hash, version](raw_message const& m) {
            if (m.heading.command() != message::compact_filter_checkpoint::command) return step::skip;
            byte_reader reader(m.payload);
            auto const parsed = message::compact_filter_checkpoint::from_data(reader, version);
            return parsed && parsed->filter_type == type && parsed->stop_hash == tip_hash ? step::done : step::skip;
        });
        if ( ! answer) {
            if (answer.error() == error::channel_timeout && ++link->silences[type] >= silences_limit) link->no_filters.insert(type);
            continue;
        }
        link->silences.erase(type);
        byte_reader reader(answer->back().payload);
        auto const parsed = message::compact_filter_checkpoint::from_data(reader, version);
        if ( ! parsed) continue;
        // One checkpoint for every whole thousand blocks of the chain: a list
        // that is short (or long) skips the checks it would carry.
        if (parsed->filter_headers.size() != wanted) {
            refuse(*link, std::to_string(parsed->filter_headers.size()) + " filter checkpoints of type " + std::to_string(type) +
                " for a chain of " + std::to_string(tip) + " blocks, not " + std::to_string(wanted));
            continue;
        }
        lists.emplace_back(link, parsed->filter_headers);
    }
    if (lists.empty()) co_return std::unexpected(error::not_found);

    // A list that differs from the first is settled at the first checkpoint
    // they part at: each peer's filter headers must lead to its own
    // checkpoint, and the first height where they differ is judged by its block.
    for (size_t other = 1; other < lists.size(); ++other) {
        auto const& a = lists.front().second;
        auto const& b = lists[other].second;
        auto const parted = size_t(std::ranges::mismatch(a, b).in1 - a.begin());
        if (parted == a.size()) continue;
        auto const first = parted == 0 ? uint32_t(0) : uint32_t(parted) * checkpoint_interval + 1;
        auto const last = uint32_t(parted + 1) * checkpoint_interval;
        auto const stop = hash_at(last);
        if ( ! stop) continue;
        auto const previous = parted == 0 ? null_hash : a[parted - 1];
        auto const leads = [&](header_answer const& answer, hash_digest const& checkpoint) {
            if ( ! answer || answer->previous_filter_header != previous || answer->filter_hashes.size() != size_t(last - first + 1)) return false;
            auto header = previous;
            for (auto const& hash : answer->filter_hashes) header = next_filter_header_from_hash(hash, header);
            return header == checkpoint;
        };
        auto const from_a = co_await filter_hashes(lists.front().first, type, first, *stop);
        auto const from_b = co_await filter_hashes(lists[other].first, type, first, *stop);
        auto const a_leads = leads(from_a, a[parted]);
        auto const b_leads = leads(from_b, b[parted]);
        auto const why = "filter headers that do not lead to its own checkpoint at " + std::to_string(last);
        if ( ! a_leads) refuse(*lists.front().first, why);
        if ( ! b_leads) refuse(*lists[other].first, why);
        if ( ! a_leads || ! b_leads) continue;
        size_t at = 0;
        while (at < from_a->filter_hashes.size() && from_a->filter_hashes[at] == from_b->filter_hashes[at]) ++at;
        if (at == from_a->filter_hashes.size()) continue;
        std::vector<std::shared_ptr<peer_link>> parties{lists.front().first, lists[other].first};
        co_await settle(type, first + uint32_t(at), std::move(parties));
    }

    // What the peers that are left agree on.
    std::optional<std::vector<hash_digest>> agreed;
    for (auto const& [link, list] : lists) {
        if (link->closed) continue;
        if ( ! agreed) {
            agreed = list;
        } else if (*agreed != list) {
            last_error = "peers disagree on the filter header checkpoints of filter type " + std::to_string(type);
            co_return std::unexpected(error::bad_stream);
        }
    }
    if ( ! agreed) co_return std::unexpected(error::not_found);
    co_return *agreed;
}

void peer_impl::drop_filters(uint32_t height, uint8_t type, std::string const& why) {
    spdlog::warn("[wallet] Filters of type {} dropped from height {}: {}", type, height, why);
    if ( ! why.empty()) {
        for (auto const& [kind, first, last, id] : served_by) {
            if (kind != type || height < first || height > last) continue;
            for (auto const& link : links) {
                if (link->id == id) refuse(*link, why);
            }
        }
    }
    std::erase_if(served_by, [height, type](auto const& range) { return std::get<0>(range) == type && std::get<2>(range) >= height; });
    ++generation;
    auto& store = type == script_type ? scripts : prefixes;
    if (store) (void)store->truncate(height);
    // What targets read from the dropped filters is read again.
    if (type == script_type && height > 0) {
        for (auto& [key, state] : targets) (void)state.rollback(height - 1);
    }
    synced = false;
    ring();
}

::asio::awaitable<void> peer_impl::cross_check(uint8_t type, uint64_t served) {
    auto& store = type == script_type ? *scripts : *prefixes;
    auto const tip = store.tip();
    if ( ! tip) co_return;
    auto const from = std::max(store.first(), (*tip / checkpoint_interval) * checkpoint_interval + 1);
    if (from > *tip) co_return;
    std::set<uint64_t> servers{served};
    for (auto const& [kind, first, last, id] : served_by) {
        if (kind == type && last >= from) servers.insert(id);
    }
    std::shared_ptr<peer_link> other;
    for (auto const& link : filter_servers(type)) {
        if ( ! servers.contains(link->id)) {
            other = link;
            break;
        }
    }
    if ( ! other) co_return;
    auto const stop = hash_at(*tip);
    if ( ! stop) co_return;
    auto const answer = co_await filter_hashes(other, type, from, *stop);
    if ( ! answer || answer->filter_hashes.size() != size_t(*tip - from + 1)) co_return;
    auto const previous = from == store.first() ? store.previous_header() : store.header(from - 1).value_or(null_hash);
    if (answer->previous_filter_header != previous) {
        // Below `from` the stored headers end at an agreed checkpoint.
        if (from != store.first()) refuse(*other, "filter headers that do not chain to the agreed checkpoint");
        co_return;
    }
    auto header = previous;
    for (uint32_t height = from; height <= *tip; ++height) {
        header = next_filter_header_from_hash(answer->filter_hashes[height - from], header);
        if (store.header(height) == header) continue;
        auto const mine = store.read(height);
        auto const theirs = co_await filter_of(other, type, height);
        if ( ! theirs || bitcoin_hash(*theirs) != answer->filter_hashes[height - from]) {
            refuse(*other, "a filter that does not match its filter header at " + std::to_string(height));
            co_return;
        }
        std::vector<data_chunk> const filters{mine ? *mine : data_chunk{}, *theirs};
        auto const fits = co_await judge(type, height, filters);
        if ( ! fits) co_return;
        auto const why = "a filter that does not fit block " + std::to_string(height);
        if ((*fits)[0] && ! (*fits)[1]) {
            refuse(*other, why);
        } else if ( ! (*fits)[0] && (*fits)[1]) {
            drop_filters(height, type, why);
        } else {
            auto const both = "filters of block " + std::to_string(height) + " that differ from another peer's and fit it as well";
            refuse(*other, both);
            drop_filters(height, type, both);
        }
        co_return;
    }
}

::asio::awaitable<std::error_code> peer_impl::sync_filters(uint8_t type) {
    auto& store = type == script_type ? *scripts : *prefixes;
    auto const tip = header_tip();
    auto const tip_hash = hash_at(tip);
    if ( ! tip_hash) co_return error::success;
    auto const birthday = std::min(settings.birthday, tip);
    auto const generation_before = generation;

    auto const agreed = co_await agreed_checkpoints(type, *tip_hash, tip);
    if ( ! agreed) co_return agreed.error() == error::not_found ? std::error_code{} : agreed.error();
    if (generation != generation_before) co_return error::success;
    auto const& checkpoints = *agreed;

    // The stored headers at each checkpoint are the agreed ones: from the
    // first that is not, the store goes back to the last verified checkpoint.
    if (auto const stored_tip = store.tip()) {
        for (uint32_t at = checkpoint_interval; at <= *stored_tip; at += checkpoint_interval) {
            auto const stored = store.header(at);
            auto const cp = at / checkpoint_interval - 1;
            if ( ! stored || (cp < checkpoints.size() && *stored == checkpoints[cp])) continue;
            drop_filters(at - checkpoint_interval + 1, type, "");
            co_return error::success;
        }
    }

    auto servers = filter_servers(type);
    if (servers.empty()) co_return error::success;
    // The first peer asked moves on every round.
    std::rotate(servers.begin(), servers.begin() + (filter_turn++ % servers.size()), servers.end());

    // A store that starts after the birthday, or holds nothing (at a
    // birthday, or cut back below its first height by a reorganization),
    // starts again at the birthday, after the filter header the peers give.
    if (store.first() > birthday || store.empty()) {
        hash_digest previous = null_hash;
        if (birthday > 0) {
            auto const before = co_await header_before(servers.front(), type, birthday - 1, checkpoints);
            if ( ! before) co_return before.error();
            previous = *before;
        }
        if (auto const ec = store.restart(birthday, previous)) co_return ec;
    }

    size_t turn = 0;
    uint64_t last_server = 0;
    while (store.next() <= tip && ! stopping) {
        if (generation != generation_before) co_return error::success;
        auto const start = store.next();
        auto const end = std::min(start + headers_per_request - 1, tip);
        auto const link = servers[turn++ % servers.size()];
        if (link->closed) co_return error::channel_stopped;
        auto const stop = hash_at(end);
        if ( ! stop) co_return error::success;
        auto hashes = co_await filter_hashes(link, type, start, *stop);
        if ( ! hashes) co_return hashes.error();
        if (hashes->previous_filter_header != store.tip_header() || hashes->filter_hashes.size() != size_t(end - start + 1)) {
            // Where the stored headers and this peer's part is not known yet:
            // the store goes back to its last verified checkpoint, and the
            // next round starts with another peer.
            last_error = "filter headers do not chain to the stored ones at height " + std::to_string(start);
            auto const verified = start == 0 ? 0 : ((start - 1) / checkpoint_interval) * checkpoint_interval + 1;
            drop_filters(std::max(store.first(), verified), type, "");
            co_return error::bad_stream;
        }

        std::vector<filter_entry> entries(end - start + 1);
        std::vector<bool> filled(entries.size(), false);
        std::map<hash_digest, size_t> position;
        std::map<hash_digest, hash_digest> filter_hash_of;
        auto header = store.tip_header();
        for (uint32_t height = start; height <= end; ++height) {
            auto const at = size_t(height - start);
            auto const block = hash_at(height);
            if ( ! block) co_return error::success;
            header = next_filter_header_from_hash(hashes->filter_hashes[at], header);
            if (height > 0 && height % checkpoint_interval == 0) {
                auto const cp = height / checkpoint_interval - 1;
                if (cp < checkpoints.size() && checkpoints[cp] != header) {
                    refuse(*link, "filter headers that miss the agreed checkpoint at " + std::to_string(height));
                    co_return error::bad_stream;
                }
            }
            entries[at].block = *block;
            entries[at].header = header;
            position[*block] = at;
            filter_hash_of[*block] = hashes->filter_hashes[at];
        }

        // Each filter is checked against its hash as it arrives, and an answer
        // holds at most filter_answer_bytes: a longer one goes on from the
        // first filter it did not hold.
        for (auto chunk = start; chunk <= end;) {
            auto const chunk_end = std::min(chunk + filters_per_request - 1, end);
            message::get_compact_filters ask{type, chunk, *hash_at(chunk_end)};
            auto const version = link->session->negotiated_version();
            struct answer_state {
                std::map<hash_digest, hash_digest> wanted;
                size_t remaining = 0;
                uint64_t bytes = 0;
                std::optional<std::string> fault;
            };
            auto state = std::make_shared<answer_state>();
            for (auto height = chunk; height <= chunk_end; ++height) {
                auto const& block = entries[height - start].block;
                state->wanted.emplace(block, filter_hash_of[block]);
            }
            state->remaining = state->wanted.size();
            auto answer = co_await request(link, ask, [type, version, state](raw_message const& m) {
                if (m.heading.command() != message::compact_filter::command) return step::skip;
                byte_reader reader(m.payload);
                auto const parsed = message::compact_filter::from_data(reader, version);
                if ( ! parsed || parsed->filter_type != type) return step::skip;
                auto const found = state->wanted.find(parsed->block_hash);
                if (found == state->wanted.end()) return step::skip;
                if (bitcoin_hash(parsed->filter) != found->second) {
                    state->fault = "a filter that does not match its filter hash";
                    return step::done;
                }
                state->wanted.erase(found);
                state->bytes += m.payload.size();
                if (--state->remaining == 0 || state->bytes > filter_answer_bytes) return step::done;
                return step::more;
            });
            if ( ! answer) co_return answer.error();
            if (state->fault) {
                refuse(*link, *state->fault);
                co_return error::bad_stream;
            }
            for (auto const& raw : *answer) {
                byte_reader reader(raw.payload);
                auto parsed = message::compact_filter::from_data(reader, version);
                if ( ! parsed) continue;
                auto const found = position.find(parsed->block_hash);
                if (found == position.end()) continue;
                entries[found->second].filter = std::move(parsed->filter);
                filled[found->second] = true;
            }
            auto next = chunk;
            while (next <= chunk_end && filled[next - start]) ++next;
            if (next == chunk) co_return error::bad_stream;
            chunk = next;
        }
        if (generation != generation_before || store.next() != start) co_return error::success;
        if (auto const ec = store.append(entries)) co_return ec;
        served_by.emplace_back(type, start, end, link->id);
        while (served_by.size() > 64) served_by.pop_front();
        last_server = link->id;
        update_status();
    }
    if (generation == generation_before) co_await cross_check(type, last_server);
    co_return error::success;
}

} // namespace kth::node::wallet::detail
