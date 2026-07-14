// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/chain/block.hpp>

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <boost/range/adaptor/reversed.hpp>

#include <kth/domain/chain/chain_state.hpp>
#include <kth/domain/chain/compact.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/deserialization.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/domain/multi_crypto_support.hpp>
#include <kth/infrastructure/config/checkpoint.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/message/message_tools.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/limits.hpp>

// Bitcoin Core optimized SHA256D64 for merkle tree computation
#include <crypto/sha256.h>

namespace kth::domain::chain {

using namespace kth::domain::machine;
using namespace boost::adaptors;

#if defined(KTH_CURRENCY_LTC)
//Litecoin mainnet genesis block
static
std::string const encoded_mainnet_genesis_block =
    "01000000"                                                                                                                                          //version
    "0000000000000000000000000000000000000000000000000000000000000000"                                                                                  //prev hash
    "d9ced4ed1130f7b7faad9be25323ffafa33232a17c3edf6cfd97bee6bafbdd97"                                                                                  //merkle root le *
    "b9aa8e4e"                                                                                                                                          //timestamp le *
    "f0ff0f1e"                                                                                                                                          //bits =
    "cd513f7c"                                                                                                                                          //nonce X
    "01"                                                                                                                                                //nro txs
    "01000000"                                                                                                                                          //version
    "01"                                                                                                                                                // inputs
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"                                                                          //prev output
    "48"                                                                                                                                                //script length
    "04ffff001d0104404e592054696d65732030352f4f63742f32303131205374657665204a6f62732c204170706c65e280997320566973696f6e6172792c2044696573206174203536"  //scriptsig
    "ffffffff"                                                                                                                                          //sequence
    "01"                                                                                                                                                //outputs
    "00f2052a01000000"                                                                                                                                  //50 btc
    "43"                                                                                                                                                //pk_script length
    "41040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9ac"            //pk_script
    "00000000";     //NOLINT                                                                                                                            //locktime

//Litecoin testnet genesis block
static
std::string const encoded_testnet_genesis_block =
    "01000000"                                                                                                                                          //version
    "0000000000000000000000000000000000000000000000000000000000000000"                                                                                  //prev hash
    "d9ced4ed1130f7b7faad9be25323ffafa33232a17c3edf6cfd97bee6bafbdd97"                                                                                  //merkle root le
    "f60ba158"                                                                                                                                          //timestamp le
    "f0ff0f1e"                                                                                                                                          //bits
    "e1790400"                                                                                                                                          //nonce
    "01"                                                                                                                                                //nro txs
    "01000000"                                                                                                                                          //version
    "01"                                                                                                                                                // inputs
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"                                                                          //prev output
    "48"                                                                                                                                                //script length
    "04ffff001d0104404e592054696d65732030352f4f63742f32303131205374657665204a6f62732c204170706c65e280997320566973696f6e6172792c2044696573206174203536"  //scriptsig
    "ffffffff"                                                                                                                                          //sequence
    "01"                                                                                                                                                //outputs
    "00f2052a01000000"                                                                                                                                  //50 btc
    "43"                                                                                                                                                //pk_script length
    "41040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9ac"            //pk_script
    "00000000";           //NOLINT                                                                                                                      //locktime
#else  //KTH_CURRENCY_LTC

static
std::string const encoded_mainnet_genesis_block =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "29ab5f49"
    "ffff001d"
    "1dac2b7c"
    "01"
    "01000000"
    "01"
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "4d"
    "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
    "ffffffff"
    "01"
    "00f2052a01000000"
    "43"
    "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
    "00000000"; //NOLINT

static
std::string const encoded_testnet_genesis_block =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "dae5494d"
    "ffff001d"
    "1aa4ae18"
    "01"
    "01000000"
    "01"
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "4d"
    "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
    "ffffffff"
    "01"
    "00f2052a01000000"
    "43"
    "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
    "00000000"; //NOLINT
#endif  //KTH_CURRENCY_LTC

#if defined(KTH_CURRENCY_BCH)
static
std::string const encoded_testnet4_genesis_block =
    "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4af1a93c5fffff001d01d3cd060101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000"; //NOLINT

static
std::string const encoded_scalenet_genesis_block =
    "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4ac6da435fffff001da4d594a20101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000"; //NOLINT

static
std::string const encoded_chipnet_genesis_block =
    "0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4af1a93c5fffff001d01d3cd060101000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac00000000"; //NOLINT

#endif

static
std::string const encoded_regtest_genesis_block =
    "01000000"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
    "dae5494d"
    "ffff7f20"
    "02000000"
    "01"
    "01000000"
    "01"
    "0000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "4d"
    "04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73"
    "ffffffff"
    "01"
    "00f2052a01000000"
    "43"
    "4104678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5fac"
    "00000000";  //NOLINT

// Constructors.
//-----------------------------------------------------------------------------

// TODO(legacy): deal with possibility of inconsistent merkle root in relation to txs.
block::block(chain::header header, transaction::list transactions)
    : header_(header)
    , transactions_(std::move(transactions))
{}


// Operators.
//-----------------------------------------------------------------------------

bool block::operator==(block const& x) const {
    return header_ == x.header_ && transactions_ == x.transactions_;
}

// Serialization.
//-----------------------------------------------------------------------------

// static
expect<block> block::from_data(byte_reader& reader) {
    auto const start_deserialize = asio::steady_clock::now();
    auto const hdr = chain::header::from_data(reader, true);
    if ( ! hdr) {
        return std::unexpected(hdr.error());
    }
    auto txs = read_collection<chain::transaction>(reader, true);
    if ( ! txs) {
        return std::unexpected(txs.error());
    }
    auto const end_deserialize = asio::steady_clock::now();
    auto res = block(*hdr, std::move(*txs));
    res.validation.start_deserialize = start_deserialize;
    res.validation.end_deserialize = end_deserialize;
    return res;
}

expect<void> block::to_data(byte_writer& writer) const {
    if (auto r = header_.to_data(writer, true); ! r) return r;
    if (auto r = writer.write_size_little_endian(transactions_.size()); ! r) return r;
    for (auto const& tx : transactions_) {
        if (auto r = tx.to_data(writer, true); ! r) return r;
    }
    return {};
}

hash_list block::to_hashes() const {
    hash_list out;
    out.reserve(transactions_.size());
    auto const to_hash = [&out](transaction const& tx) {
        out.push_back(tx.hash());
    };

    // Hash ordering matters, don't use std::transform here.
    std::for_each(transactions_.begin(), transactions_.end(), to_hash);
    return out;
}

// Properties (size, accessors).
//-----------------------------------------------------------------------------

// Full block serialization is always canonical encoding.
size_t block::serialized_size() const {
    return chain::serialized_size(*this);
}

chain::header const& block::header() const {
    return header_;
}

transaction::list& block::transactions() {
    return transactions_;
}

transaction::list const& block::transactions() const {
    return transactions_;
}

void block::set_transactions(transaction::list const& value) {
    transactions_ = value;
}

void block::set_transactions(transaction::list&& value) {
    transactions_ = std::move(value);
}

// Convenience property.
hash_digest block::hash() const {
    return chain::hash(header_);
}

// Utilities.
//-----------------------------------------------------------------------------

chain::block genesis_generic(std::string const& raw_data) {
    auto data = decode_base16(raw_data);
    KTH_ASSERT(data);

    byte_reader reader(*data);
    auto genesis = block::from_data(reader);

    KTH_ASSERT(genesis);
    KTH_ASSERT(genesis->transactions().size() == 1);
    KTH_ASSERT(genesis->generate_merkle_root() == genesis->header().merkle());

    return *genesis;
}

chain::block block::genesis_mainnet() {
    return genesis_generic(encoded_mainnet_genesis_block);
}

chain::block block::genesis_testnet() {
    return genesis_generic(encoded_testnet_genesis_block);
}

chain::block block::genesis_regtest() {
    return genesis_generic(encoded_regtest_genesis_block);
}

#if defined(KTH_CURRENCY_BCH)
chain::block block::genesis_testnet4() {
    return genesis_generic(encoded_testnet4_genesis_block);
}

chain::block block::genesis_scalenet() {
    return genesis_generic(encoded_scalenet_genesis_block);
}

chain::block block::genesis_chipnet() {
    return genesis_generic(encoded_chipnet_genesis_block);
}
#endif

// With a 32 bit chain the size of the result should not exceed 43 and with a
// 64 bit chain should not exceed 75, using a limit of: 10 + log2(height) + 1.
size_t block::locator_size(size_t top) {
    auto const first_ten_or_top = std::min(size_t{10}, top);
    auto const remaining = top - first_ten_or_top;

    // Set log2(0) -> 0, log2(1) -> 1 and round up higher exponential backoff
    // results to next whole number by adding 0.5 and truncating.
    if (remaining < 2) {
        return top + size_t{1};
    }
    return first_ten_or_top + size_t(std::log2(remaining) + 0.5) + size_t{1};
}

// This algorithm is a network best practice, not a consensus rule.
block::indexes block::locator_heights(size_t top) {
    size_t step = 1;
    block::indexes heights;
    auto const reservation = locator_size(top);
    heights.reserve(reservation);

    // Start at the top of the chain and work backwards to zero.
    for (auto height = top; height > 0; height = floor_subtract(height, step)) {
        // Push top 10 indexes first, then back off exponentially.
        if (heights.size() >= 10) {
            step <<= 1U;
        }

        heights.push_back(height);
    }

    // Push the genesis block index.
    heights.push_back(0);

    // Validate the reservation computation.
    KTH_ASSERT(heights.size() <= reservation);
    return heights;
}

// Validation helpers.
//-----------------------------------------------------------------------------

// [GetBlockProof]
uint256_t block::proof() const {
    return header_.proof();
}

// static
uint64_t block::subsidy(size_t height, bool retarget) {
    static auto const overflow = sizeof(uint64_t) * byte_bits;
    auto const halvings = height / subsidy_interval(retarget);
    // Past the 64th halving the shift `initial >> halvings` would be UB
    // (exponent ≥ type width). The previous guard `>>= (halvings >= 64 ?
    // 0 : halvings)` sidestepped the UB but ran `>>= 0` instead, which
    // is a no-op — the initial subsidy stuck around forever, silently
    // restarting the emission clock. Return 0 explicitly for
    // `halvings >= 64` so the schedule terminates at zero.
    if (halvings >= overflow) {
        return 0;
    }
    return initial_block_subsidy_satoshi() >> halvings;
}

// Returns max_size_t in case of overflow.
size_t block::signature_operations(bool bip16, bool bip141) const {
    bip141 = false;  // No segwit
    auto const value = [bip16, bip141](size_t total, transaction const& tx) {
        return ceiling_add(total, tx.signature_operations(bip16, bip141));
    };

    //*************************************************************************
    // CONSENSUS: Legacy sigops are counted in coinbase scripts despite the
    // fact that coinbase input scripts are never executed. There is no need
    // to exclude p2sh coinbase sigops since there is never a script to count.
    //*************************************************************************
    auto const& txs = transactions_;
    return std::accumulate(txs.begin(), txs.end(), size_t{0}, value);
}

// Returns max_size_t in case of overflow or unpopulated chain state.
size_t block::signature_operations() const {
    auto const state = validation.state;
    auto const bip16 = state ? state->is_enabled(script_flags::bip16_rule) : true;
    auto const bip141 = false;

    return signature_operations(bip16, bip141);
}

size_t block::total_inputs(bool with_coinbase) const {
    return chain::total_inputs(*this, with_coinbase);
}

// True if there is another coinbase other than the first tx.
// No txs or coinbases returns false.
bool block::is_extra_coinbases() const {
    if (transactions_.empty()) {
        return false;
    }

    auto const value = [](transaction const& tx) {
        return tx.is_coinbase();
    };

    auto const& txs = transactions_;
    return std::any_of(txs.begin() + 1, txs.end(), value);
}

bool block::is_final(size_t height, uint32_t block_time) const {
    auto const value = [=](transaction const& tx) {
        return tx.is_final(height, block_time);
    };

    auto const& txs = transactions_;
    return std::all_of(txs.begin(), txs.end(), value);
}

// Distinctness is defined by transaction hash.
bool block::is_distinct_transaction_set() const {
    auto const hasher = [](transaction const& tx) { return tx.hash(); };
    auto const& txs = transactions_;
    hash_list hashes(txs.size());
    std::transform(txs.begin(), txs.end(), hashes.begin(), hasher);
    std::sort(hashes.begin(), hashes.end());
    auto const distinct_end = std::unique(hashes.begin(), hashes.end());
    return distinct_end == hashes.end();
}

hash_digest block::generate_merkle_root() const {
    if (transactions_.empty()) {
        return null_hash;
    }

    auto hashes = to_hashes();

    // Temporary buffers for batched SHA256D64 processing
    // SHA256D64 uses SIMD: 2-way on ARM, 4-way on SSE4.1, 8-way on AVX2
    std::vector<uint8_t> batch_input;
    std::vector<uint8_t> batch_output;

    while (hashes.size() > 1) {
        // Bitcoin merkle: duplicate last hash if odd count at this level
        if (hashes.size() % 2 != 0) {
            hashes.push_back(hashes.back());
        }

        size_t const num_pairs = hashes.size() / 2;

        // Prepare batch input: all pairs concatenated (64 bytes each)
        batch_input.resize(num_pairs * 64);
        batch_output.resize(num_pairs * 32);

        for (size_t i = 0; i < num_pairs; ++i) {
            std::copy(hashes[i * 2].begin(), hashes[i * 2].end(),
                      batch_input.begin() + i * 64);
            std::copy(hashes[i * 2 + 1].begin(), hashes[i * 2 + 1].end(),
                      batch_input.begin() + i * 64 + 32);
        }

        // Process all pairs at once using Bitcoin Core's optimized SHA256D64
        SHA256D64(batch_output.data(), batch_input.data(), num_pairs);

        // Copy results back to hashes vector
        hashes.resize(num_pairs);
        for (size_t i = 0; i < num_pairs; ++i) {
            std::copy(batch_output.begin() + i * 32,
                      batch_output.begin() + (i + 1) * 32,
                      hashes[i].begin());
        }
    }

    return hashes.front();
}

size_t block::non_coinbase_input_count() const {
    if (transactions_.empty()) {
        return 0;
    }

    auto const counter = [](size_t sum, transaction const& tx) {
        return sum + tx.inputs().size();
    };

    auto const& txs = transactions_;
    return std::accumulate(txs.begin() + 1, txs.end(), size_t(0), counter);
}


//Note(kth): LTOR (Legacy Transaction ORdering) is a check just for Bitcoin (BTC)
//               and for BitcoinCash (BCH) before 2018-Nov-15.
//****************************************************************************
// CONSENSUS: This is only necessary because satoshi stores and queries as it
// validates, imposing an otherwise unnecessary partial transaction ordering.
//*****************************************************************************
bool block::is_forward_reference() const {
    std::unordered_map<hash_digest, bool> hashes(transactions_.size());
    auto const is_forward = [&hashes](input const& input) {
        return hashes.count(input.previous_output().hash()) != 0;
    };

    for (auto const& tx : reverse(transactions_)) {
        hashes.emplace(tx.hash(), true);

        if (std::any_of(tx.inputs().begin(), tx.inputs().end(), is_forward)) {
            return true;
        }
    }

    return false;
}

bool block::is_canonical_ordered() const {
    //precondition: transactions_.size() > 1

    auto const hash_cmp = [](transaction const& a, transaction const& b){
        return std::lexicographical_compare(a.hash().rbegin(), a.hash().rend(), b.hash().rbegin(), b.hash().rend());
    };

    // Skip the coinbase
    return std::is_sorted(transactions_.begin() + 1, transactions_.end(), hash_cmp);
}

// This is an early check that is redundant with block pool accept checks.
bool block::is_internal_double_spend() const {
    if (transactions_.empty()) {
        return false;
    }

    point::list outs;
    outs.reserve(non_coinbase_input_count());
    auto const& txs = transactions_;

    // Merge the prevouts of all non-coinbase transactions into one set.
    for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx) {
        auto out = tx->previous_outputs();
        std::move(out.begin(), out.end(), std::inserter(outs, outs.end()));
    }

    std::sort(outs.begin(), outs.end());
    auto const distinct_end = std::unique(outs.begin(), outs.end());
    auto const distinct = (distinct_end == outs.end());
    return !distinct;
}

bool block::is_valid_merkle_root() const {
    return generate_merkle_root() == header_.merkle();
}

// Overflow returns max_uint64.
uint64_t block::fees() const {
    ////static_assert(max_money() < max_uint64, "overflow sentinel invalid");
    auto const value = [](uint64_t total, transaction const& tx) {
        return ceiling_add(total, tx.fees());
    };

    auto const& txs = transactions_;
    return std::accumulate(txs.begin(), txs.end(), uint64_t{0}, value);
}

uint64_t block::claim() const {
    return transactions_.empty() ? 0 : transactions_.front().total_output_value();
}

// Overflow returns max_uint64.
uint64_t block::reward(size_t height) const {
    ////static_assert(max_money() < max_uint64, "overflow sentinel invalid");
    return ceiling_add(fees(), subsidy(height, true));
}

bool block::is_valid_coinbase_claim(size_t height) const {
    return claim() <= reward(height);
}

bool block::is_valid_coinbase_script(size_t height) const {
    if (transactions_.empty() || transactions_.front().inputs().empty()) {
        return false;
    }

    auto const& script = transactions_.front().inputs().front().script();
    return script::is_coinbase_pattern(script.operations(), height);
}

code block::check_transactions() const {
    constexpr size_t max_block_size = static_absolute_max_block_size();
    code ec;
    for (auto const& tx : transactions_) {
        if ((ec = tx.check(max_block_size, false))) {
            return ec;
        }
    }
    return error::success;
}

// Validation.
//-----------------------------------------------------------------------------

// Structural validation — no context needed, no prevout cache needed.
code block::check() const {
    validation.start_check = asio::steady_clock::now();

    code ec;
    if ((ec = header_.check(chain::hash(header_), false))) {
        return ec;
    }
    return check_body();
}

// Check block body only (skip header validation for headers-first sync).
// Use this when headers have already been validated during header sync.
code block::check_body() const {
    validation.start_check = asio::steady_clock::now();

    if (serialized_size() > static_absolute_max_block_size()) {
        return error::block_size_limit;
    }

    if (transactions_.empty()) {
        return error::empty_block;
    }

    if ( ! transactions_.front().is_coinbase()) {
        return error::first_not_coinbase;
    }

    if (is_extra_coinbases()) {
        return error::extra_coinbases;
    }

#if ! defined(KTH_CURRENCY_BCH) // BTC and LTC
    // Note(kth): LTOR (Legacy Transaction ORdering) is a check just for Bitcoin (BTC)
    //            and for BitcoinCash (BCH) before 2018-Nov-15.
    if (is_forward_reference()) {
        return error::forward_reference;
    }
#endif

    if (is_internal_double_spend()) {
        return error::block_internal_double_spend;
    }

    if ( ! is_valid_merkle_root()) {
        return error::merkle_mismatch;
    }

    return check_transactions();
}

// Non-member functions.
//-----------------------------------------------------------------------------

// With a 32 bit chain the size of the result should not exceed 43 and with a
// 64 bit chain should not exceed 75, using a limit of: 10 + log2(height) + 1.
size_t locator_size(size_t top) {
    // Set rounding behavior, not consensus-related, thread side effect :<.
    std::fesetround(FE_UPWARD);

    auto const first_ten_or_top = std::min(size_t(10), top);
    auto const remaining = top - first_ten_or_top;
    auto const back_off = remaining == 0 ? 0.0 : remaining == 1 ? 1.0 : std::log2(remaining);
    auto const rounded_up_log = size_t(std::nearbyint(back_off));
    return first_ten_or_top + rounded_up_log + size_t(1);
}

// This algorithm is a network best practice, not a consensus rule.
// static
indexes locator_heights(size_t top) {
    size_t step = 1;
    indexes heights;
    auto const reservation = locator_size(top);
    heights.reserve(reservation);

    // Start at the top of the chain and work backwards to zero.
    for (auto height = top; height > 0; height = floor_subtract(height, step)) {
        // Push top 10 indexes first, then back off exponentially.
        if (heights.size() >= 10) {
            step <<= 1U;
        }

        heights.push_back(height);
    }

    // Push the genesis block index.
    heights.push_back(0);

    // Validate the reservation computation.
    KTH_ASSERT(heights.size() <= reservation);
    return heights;
}

size_t total_inputs(block const& blk, bool with_coinbase) {
    auto const inputs = [](size_t total, transaction const& tx) {
        return *safe_add(total, tx.inputs().size());
    };

    auto const& txs = blk.transactions();
    size_t const offset = with_coinbase ? 0 : 1;
    return std::accumulate(txs.begin() + offset, txs.end(), size_t(0), inputs);
}

// Full block serialization is always canonical encoding.
size_t serialized_size(block const& blk) {
    auto const sum = [](size_t total, transaction const& tx) {
        return *safe_add(total, tx.serialized_size(true));
    };

    auto const& txs = blk.transactions();
    return blk.header().serialized_size(true) +
           infrastructure::message::variable_uint_size(txs.size()) +
           std::accumulate(txs.begin(), txs.end(), size_t(0), sum);
}

} // namespace kth::domain::chain
