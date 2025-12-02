// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utility.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <kth/node.hpp>

namespace kth::node::test {

using namespace kth::blockchain;
using namespace kth::domain::chain;
using namespace kth::domain::config;

infrastructure::config::checkpoint const check0 {
    null_hash, 0
};

infrastructure::config::checkpoint const check42 {
    "4242424242424242424242424242424242424242424242424242424242424242", 42
};

const infrastructure::config::checkpoint::list no_checks;
const infrastructure::config::checkpoint::list one_check{ check42 };

// Create a headers message of specified size, starting with a genesis header.
domain::message::headers::ptr message_factory(size_t count) {
    return message_factory(count, null_hash);
}

// Create a headers message of specified size, using specified previous hash.
domain::message::headers::ptr message_factory(size_t count,
    hash_digest const& previous) {
    auto previous_hash = previous;
    auto const headers = std::make_shared<domain::message::headers>();
    auto& elements = headers->elements();

    for (size_t height = 0; height < count; ++height) {
        header const current_header{ 0, previous_hash, {}, 0, 0, 0 };
        elements.push_back(current_header);
        previous_hash = current_header.hash();
    }

    return headers;
}

reservation_fixture::reservation_fixture(reservations& reservations,
    size_t slot, uint32_t sync_timeout_seconds, clock::time_point now)
    : reservation(reservations, slot, sync_timeout_seconds)
    , now_(now)
{}

// Accessor
std::chrono::microseconds reservation_fixture::rate_window() const {
    return reservation::rate_window();
}

// Accessor
bool reservation_fixture::pending() const {
    return reservation::pending();
}

// Accessor
void reservation_fixture::set_pending(bool value) {
    reservation::set_pending(value);
}

// Stub
std::chrono::high_resolution_clock::time_point reservation_fixture::now() const {
    return now_;
}

// ----------------------------------------------------------------------------

blockchain_fixture::blockchain_fixture(bool import_result, size_t gap_trigger, size_t gap_height)
    : import_result_(import_result)
    , gap_trigger_(gap_trigger)
    , gap_height_(gap_height)
{}

bool blockchain_fixture::get_gap_range(size_t& out_first, size_t& out_last) const {
    return false;
}

bool blockchain_fixture::get_next_gap(size_t& out_height, size_t start_height) const {
    if (start_height == gap_trigger_) {
        out_height = gap_height_;
        return true;
    }

    return false;
}

bool blockchain_fixture::get_block_exists(hash_digest const& block_hash) const {
    return false;
}

bool blockchain_fixture::get_fork_work(uint256_t& out_difficulty, size_t height) const {
    return false;
}

bool blockchain_fixture::get_header(header& out_header, size_t height) const {
    return false;
}

bool blockchain_fixture::get_height(size_t& out_height, hash_digest const& block_hash) const {
    return false;
}

bool blockchain_fixture::get_bits(uint32_t& out_bits, size_t const& height) const {
    return false;
}

bool blockchain_fixture::get_timestamp(uint32_t& out_timestamp, size_t const& height) const {
    return false;
}

bool blockchain_fixture::get_version(uint32_t& out_version, size_t const& height) const {
    return false;
}

bool blockchain_fixture::get_last_height(size_t& out_height) const {
    return false;
}

bool blockchain_fixture::get_output(domain::chain::output& out_output,
    size_t& out_height, size_t& out_position,
    const domain::chain::output_point& outpoint) const {
    return false;
}

bool blockchain_fixture::get_spender_hash(hash_digest& out_hash, output_point const& outpoint) const {
    return false;
}

bool blockchain_fixture::get_is_unspent_transaction(hash_digest const& transaction_hash) const {
    return false;
}

bool blockchain_fixture::get_transaction_height(size_t& out_block_height, hash_digest const& transaction_hash) const {
    return false;
}

transaction_ptr blockchain_fixture::get_transaction(size_t& out_block_height, hash_digest const& transaction_hash) const {
    return nullptr;
}

bool blockchain_fixture::stub(header_const_ptr header, size_t height) {
    return false;
}

bool blockchain_fixture::fill(block_const_ptr block, size_t height) {
    // This prevents a zero import cost, which is useful in testing timeout.
    std::this_thread::sleep_for(std::chrono::microseconds(1));
    return import_result_;
}

bool blockchain_fixture::push(block_const_ptr_list const& blocks,
    size_t height) {
    return false;
}

bool blockchain_fixture::pop(block_const_ptr_list& blocks,
    hash_digest const& fork_hash) {
    return false;
}

} // namespace kth::node::test
