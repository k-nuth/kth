// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/math/mmr.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/data.hpp>

#include "mmr_vectors.hpp"

using namespace kth;
using namespace kd;

namespace {

hash_digest display_hash(char const* text) {
    auto const hash = decode_hash(text);
    REQUIRE(hash);
    return *hash;
}

hash_list display_hashes(std::span<char const* const> texts) {
    hash_list hashes;
    for (auto const* text : texts) {
        hashes.push_back(display_hash(text));
    }
    return hashes;
}

mmr build_mmr(hash_list const& hashes) {
    mmr result;
    hash_digest previous = null_hash;
    for (auto const& hash : hashes) {
        result.append(hash, previous);
        previous = hash;
    }
    return result;
}

mmr_accumulator build_accumulator(hash_list const& hashes) {
    mmr_accumulator result;
    for (auto const& hash : hashes) {
        REQUIRE(result.extend(hash));
    }
    return result;
}

// Show that an attack reaches the advertised root before testing the guard.
hash_digest unguarded_root(uint64_t index, hash_digest leaf, hash_list const& branch) {
    for (auto const& sibling : branch) {
        leaf = (index & 1) != 0
            ? bitcoin_hash(splice(sibling, leaf))
            : bitcoin_hash(splice(leaf, sibling));
        index >>= 1;
    }
    return leaf;
}

} // namespace

TEST_CASE("mmr official block roots and Electrum proofs", "[shv]") {
    namespace vectors = kth::test::shv;
    for (auto const& vector : vectors::blocks) {
        INFO("block " << vector.height);
        auto const hashes = display_hashes(vector.hashes);
        auto const server = build_mmr(hashes);
        auto const client = build_accumulator(hashes);
        auto const expected = display_hash(vector.root);
        REQUIRE(server.root(hashes.size(), hashes.back()) == expected);
        REQUIRE(client.root() == expected);
        REQUIRE(server.peaks(hashes.size(), hashes.back()) == client.peaks());
    }

    auto const hashes = display_hashes(vectors::first_16);
    auto const server = build_mmr(hashes);
    for (auto const& vector : vectors::proofs) {
        INFO(vector.name);
        auto const raw = decode_base16(vector.header);
        REQUIRE(raw);
        REQUIRE(raw->size() == 80);
        auto const leaf = bitcoin_hash(*raw);
        REQUIRE(leaf == hashes[vector.index]);
        auto const branch = display_hashes(vector.branch);
        auto const expected = display_hash(vector.root);
        auto const prefix = hash_list(hashes.begin(), hashes.begin() + vector.leaf_count);
        auto const client = build_accumulator(prefix);
        REQUIRE(client.root() == expected);
        REQUIRE(client.verify_proof_to_root(vector.index, leaf, branch));
        REQUIRE(verify_mmr_proof(vector.index, vector.leaf_count, leaf, branch, expected));
        REQUIRE(server.proof_to_root(vector.index, vector.leaf_count,
                    hashes[vector.index ^ 1], prefix.back()) == branch);
        REQUIRE(server.root(vector.leaf_count, prefix.back()) == expected);

        if (vector.index == vector.leaf_count - 1) {
            auto const bootstrapped = mmr_accumulator::bootstrap(
                vector.leaf_count, leaf, branch, expected);
            REQUIRE(bootstrapped);
            REQUIRE(*bootstrapped == client);
        }
    }
}

TEST_CASE("mmr historical proofs and roots match Knuth merkle construction", "[shv]") {
    chain::transaction::list transactions;
    hash_list hashes;
    mmr server;
    mmr_accumulator client;
    std::vector<mmr_accumulator> states{client};
    REQUIRE(chain::block{}.generate_merkle_root() == client.root());
    for (uint32_t i = 0; i < 33; ++i) {
        // These structural transactions give both constructions the same leaves.
        transactions.emplace_back(1, i, chain::transaction::ins{}, chain::transaction::outs{});
        auto const leaf = transactions.back().hash();
        server.append(leaf, hashes.empty() ? null_hash : hashes.back());
        hashes.push_back(leaf);
        REQUIRE(client.extend(leaf));
        states.push_back(client);
        chain::block const block{chain::header{}, transactions};
        REQUIRE(block.to_hashes() == hashes);
        REQUIRE(block.generate_merkle_root() == client.root());
        REQUIRE(server.root(hashes.size(), leaf) == client.root());
        REQUIRE(server.node_count() == hashes.size() - std::popcount(uint64_t(hashes.size())));
    }

    // Query every earlier commitment after the server has already reached its tip.
    for (uint64_t n = 1; n <= hashes.size(); ++n) {
        INFO("commitment size " << n);
        auto const& state = states[n];
        REQUIRE(server.peaks(n, hashes[n - 1]) == state.peaks());
        REQUIRE(server.root(n, hashes[n - 1]) == state.root());
        for (uint64_t i = 0; i < n; ++i) {
            INFO("leaf " << i);
            auto const sibling = (i ^ 1) < n ? hashes[i ^ 1] : null_hash;
            auto const peak_proof = server.proof_to_peak(i, n, sibling);
            auto const root_proof = server.proof_to_root(i, n, sibling, hashes[n - 1]);
            REQUIRE(peak_proof);
            REQUIRE(root_proof);
            REQUIRE(root_proof->size() == std::bit_width(n - 1));
            REQUIRE(peak_proof->size() <= root_proof->size());
            REQUIRE(std::equal(peak_proof->begin(), peak_proof->end(), root_proof->begin()));
            REQUIRE(state.verify_proof_to_peak(i, hashes[i], *peak_proof));
            REQUIRE(state.verify_proof_to_root(i, hashes[i], *root_proof));
            REQUIRE(verify_mmr_proof(i, n, hashes[i], *root_proof, state.root()));

            auto wrong_leaf = hashes[i];
            wrong_leaf[0] ^= 1;
            REQUIRE_FALSE(state.verify_proof_to_peak(i, wrong_leaf, *peak_proof));
            REQUIRE_FALSE(state.verify_proof_to_root(i, wrong_leaf, *root_proof));
            auto extra_peak = *peak_proof;
            extra_peak.push_back(null_hash);
            REQUIRE_FALSE(state.verify_proof_to_peak(i, hashes[i], extra_peak));
            auto extra_root = *root_proof;
            extra_root.push_back(null_hash);
            REQUIRE_FALSE(state.verify_proof_to_root(i, hashes[i], extra_root));
            if ( ! peak_proof->empty()) {
                auto short_peak = *peak_proof;
                short_peak.pop_back();
                REQUIRE_FALSE(state.verify_proof_to_peak(i, hashes[i], short_peak));
            }
            if ( ! root_proof->empty()) {
                auto short_root = *root_proof;
                short_root.pop_back();
                REQUIRE_FALSE(state.verify_proof_to_root(i, hashes[i], short_root));
                auto changed_root = *root_proof;
                changed_root.front()[0] ^= 1;
                REQUIRE_FALSE(state.verify_proof_to_root(i, hashes[i], changed_root));
            }
        }

        auto const proof = server.proof_to_root(n - 1, n,
            ((n - 1) ^ 1) < n ? hashes[(n - 1) ^ 1] : null_hash, hashes[n - 1]);
        REQUIRE(proof);
        auto const bootstrapped = mmr_accumulator::bootstrap(n, hashes[n - 1], *proof, state.root());
        REQUIRE(bootstrapped);
        REQUIRE(*bootstrapped == state);
        auto extended = *bootstrapped;
        REQUIRE(extended.extend(hashes.front()));
        auto expected = state;
        REQUIRE(expected.extend(hashes.front()));
        REQUIRE(extended == expected);
    }
}

TEST_CASE("mmr removal restores exact states and permits a different continuation", "[shv]") {
    auto hashes = display_hashes(kth::test::shv::first_16);
    // Cross both 16- and 32-leaf boundaries during removal.
    for (uint8_t i = 16; i < 35; ++i) {
        hash_digest hash{};
        hash[0] = i;
        hashes.push_back(hash);
    }
    std::vector<mmr> states(1);
    for (size_t i = 0; i < hashes.size(); ++i) {
        auto next = states.back();
        next.append(hashes[i], i == 0 ? null_hash : hashes[i - 1]);
        states.push_back(next);
    }
    auto const full = states.back();
    for (size_t retained = 0; retained <= hashes.size(); ++retained) {
        auto rewound = full;
        REQUIRE(rewound.remove(hashes.size() - retained));
        REQUIRE(rewound == states[retained]);
        REQUIRE(rewound.node_count() == retained - std::popcount(uint64_t(retained)));
        REQUIRE(rewound.remove(0));
        REQUIRE(rewound == states[retained]);

        hash_digest replacement{};
        replacement[0] = 0xff;
        auto prefix = hash_list(hashes.begin(), hashes.begin() + retained);
        rewound.append(replacement, prefix.empty() ? null_hash : prefix.back());
        prefix.push_back(replacement);
        REQUIRE(rewound == build_mmr(prefix));
        REQUIRE(rewound.root(prefix.size(), replacement) == build_accumulator(prefix).root());
    }
    auto unchanged = full;
    REQUIRE_FALSE(unchanged.remove(hashes.size() + 1));
    REQUIRE(unchanged == full);
    REQUIRE_FALSE(unchanged.remove(std::numeric_limits<uint64_t>::max()));
    REQUIRE(unchanged == full);
}

TEST_CASE("mmr rejects all official duplicate subtree attacks", "[shv]") {
    namespace vectors = kth::test::shv;
    auto const real = display_hashes(vectors::first_16);
    auto const forged = display_hashes(vectors::first_16_forged);
    auto const expected = display_hash(vectors::attack_root);
    auto const honest = build_accumulator(hash_list(real.begin(), real.begin() + vectors::real_leaf_count));
    auto const attacker = build_accumulator(forged);
    REQUIRE(honest.root() == expected);
    REQUIRE(attacker.root() == expected);
    REQUIRE(honest.peaks() != attacker.peaks());
    REQUIRE(build_mmr(forged).root(vectors::forged_leaf_count) == expected);
    for (auto const& mapping : vectors::forged_proof_mappings) {
        INFO("phantom position " << mapping.forged_index);
        auto const found = std::find_if(std::begin(vectors::proofs), std::end(vectors::proofs),
            [&](auto const& proof) { return std::string_view(proof.name) == mapping.proof_name; });
        REQUIRE(found != std::end(vectors::proofs));
        auto const branch = display_hashes(found->branch);
        auto const leaf = real[mapping.real_index];
        REQUIRE(forged[mapping.forged_index] == leaf);
        REQUIRE(unguarded_root(mapping.forged_index, leaf, branch) == expected);
        REQUIRE_FALSE(verify_mmr_proof(mapping.forged_index, vectors::forged_leaf_count,
            leaf, branch, expected));
        REQUIRE_FALSE(attacker.verify_proof_to_root(mapping.forged_index, leaf, branch));
    }
    auto const forged_server = build_mmr(forged);
    auto const last_proof = forged_server.proof_to_root(15, 16, forged[14]);
    REQUIRE(last_proof);
    REQUIRE_FALSE(mmr_accumulator::bootstrap(16, forged.back(), *last_proof, expected));
}

TEST_CASE("mmr invalid bounds and malformed accumulator states are refused", "[shv]") {
    mmr empty;
    mmr_accumulator client;
    REQUIRE(empty.leaf_count() == 0);
    REQUIRE(empty.node_count() == 0);
    REQUIRE(empty.root(0) == null_hash);
    REQUIRE(empty.peaks(0) == hash_list{});
    REQUIRE(client.leaf_count() == 0);
    REQUIRE(client.peaks().empty());
    REQUIRE_FALSE(empty.root(1));
    REQUIRE_FALSE(empty.peaks(1));
    REQUIRE_FALSE(empty.proof_to_peak(0, 0));
    REQUIRE_FALSE(empty.proof_to_root(0, 0));
    REQUIRE_FALSE(empty.proof_to_peak(0, 1));
    REQUIRE_FALSE(empty.proof_to_root(0, 1));
    REQUIRE_FALSE(client.verify_proof_to_peak(0, null_hash, {}));
    REQUIRE_FALSE(client.verify_proof_to_root(0, null_hash, {}));
    REQUIRE_FALSE(verify_mmr_proof(0, 0, null_hash, {}, null_hash));
    REQUIRE_FALSE(mmr_accumulator::from_peaks(0, {null_hash}));
    REQUIRE_FALSE(mmr_accumulator::from_peaks(1, {}));
    REQUIRE_FALSE(mmr_accumulator::from_peaks(3, {null_hash}));
    auto const empty_state = mmr_accumulator::from_peaks(0, {});
    REQUIRE(empty_state);
    REQUIRE(*empty_state == client);
    auto const empty_bootstrap = mmr_accumulator::bootstrap(0, null_hash, {}, null_hash);
    REQUIRE(empty_bootstrap);
    REQUIRE(*empty_bootstrap == client);
    REQUIRE_FALSE(mmr_accumulator::bootstrap(0, null_hash, {null_hash}, null_hash));
    hash_digest nonzero{};
    nonzero[0] = 1;
    REQUIRE_FALSE(mmr_accumulator::bootstrap(0, null_hash, {}, nonzero));

    auto const hashes = display_hashes(kth::test::shv::first_16);
    auto const server = build_mmr(hashes);
    REQUIRE_FALSE(server.root(17));
    REQUIRE_FALSE(server.peaks(17));
    REQUIRE_FALSE(server.proof_to_peak(0, 17));
    REQUIRE_FALSE(server.proof_to_root(0, 17));
    REQUIRE_FALSE(server.proof_to_peak(16, 16));
    REQUIRE_FALSE(server.proof_to_root(16, 16));
    client = build_accumulator(hashes);
    REQUIRE_FALSE(client.verify_proof_to_peak(16, hashes.back(), {}));
    REQUIRE_FALSE(client.verify_proof_to_root(16, hashes.back(), {}));
    REQUIRE_FALSE(client.verify_proof_to_peak(std::numeric_limits<uint64_t>::max(), hashes.back(), {}));

    auto maximum = mmr_accumulator::from_peaks(std::numeric_limits<uint64_t>::max(), hash_list(64, nonzero));
    REQUIRE(maximum);
    auto const snapshot = *maximum;
    REQUIRE_FALSE(maximum->extend(nonzero));
    REQUIRE(*maximum == snapshot);
}

TEST_CASE("mmr bootstrap rejects corrupt paths and noncanonical padding", "[shv]") {
    auto const hashes = display_hashes(kth::test::shv::first_16);
    auto const server = build_mmr(hashes);
    auto const proof = server.proof_to_root(10, 11, null_hash, hashes[10]);
    REQUIRE(proof);
    auto const expected = server.root(11, hashes[10]);
    REQUIRE(expected);
    auto wrong_root = *expected;
    wrong_root[0] ^= 1;
    REQUIRE_FALSE(verify_mmr_proof(10, 11, hashes[10], *proof, wrong_root));
    REQUIRE_FALSE(mmr_accumulator::bootstrap(11, hashes[10], *proof, wrong_root));
    auto short_proof = *proof;
    short_proof.pop_back();
    REQUIRE_FALSE(mmr_accumulator::bootstrap(11, hashes[10], short_proof, *expected));
    auto long_proof = *proof;
    long_proof.push_back(null_hash);
    REQUIRE_FALSE(mmr_accumulator::bootstrap(11, hashes[10], long_proof, *expected));
    REQUIRE_FALSE(mmr_accumulator::bootstrap(1, hashes.front(), {null_hash}, hashes.front()));
    REQUIRE_FALSE(mmr_accumulator::bootstrap(1, hashes.front(), {}, wrong_root));
    REQUIRE_FALSE(mmr_accumulator::bootstrap(2, hashes.front(), {}, wrong_root));

    // Last-leaf padding must duplicate the running hash, even for an attacker root.
    auto altered = *proof;
    altered.front()[0] ^= 1;
    auto const altered_root = unguarded_root(10, hashes[10], altered);
    REQUIRE_FALSE(verify_mmr_proof(10, 11, hashes[10], altered, altered_root));
    REQUIRE_FALSE(mmr_accumulator::bootstrap(11, hashes[10], altered, altered_root));
    auto duplicate_left = *proof;
    auto current = bitcoin_hash(splice(hashes[10], hashes[10]));
    duplicate_left[1] = current;
    auto const duplicate_root = unguarded_root(10, hashes[10], duplicate_left);
    REQUIRE_FALSE(mmr_accumulator::bootstrap(11, hashes[10], duplicate_left, duplicate_root));
}

TEST_CASE("mmr accumulator handles the highest leaf count bits without overflow", "[shv]") {
    hash_digest leaf{};
    leaf[0] = 1;
    auto const power = uint64_t{1} << 63;
    hash_list const mountain_branch(63, null_hash);
    auto const mountain_root = unguarded_root(power - 1, leaf, mountain_branch);
    auto const mountain = mmr_accumulator::bootstrap(power, leaf, mountain_branch, mountain_root);
    REQUIRE(mountain);
    REQUIRE(mountain->leaf_count() == power);
    REQUIRE(mountain->peaks() == hash_list{mountain_root});
    REQUIRE(mountain->root() == mountain_root);
    REQUIRE(mountain->verify_proof_to_peak(power - 1, leaf, mountain_branch));
    REQUIRE(mountain->verify_proof_to_root(power - 1, leaf, mountain_branch));
    auto extended = *mountain;
    REQUIRE(extended.extend(leaf));
    REQUIRE(extended.leaf_count() == power + 1);
    REQUIRE(extended.peaks().size() == 2);

    auto const maximum = std::numeric_limits<uint64_t>::max();
    hash_list last_branch(64, null_hash);
    last_branch.front() = leaf;
    auto const expected = unguarded_root(maximum - 1, leaf, last_branch);
    auto bootstrapped = mmr_accumulator::bootstrap(maximum, leaf, last_branch, expected);
    REQUIRE(bootstrapped);
    REQUIRE(bootstrapped->leaf_count() == maximum);
    REQUIRE(bootstrapped->peaks().size() == 64);
    REQUIRE(bootstrapped->root() == expected);
    REQUIRE(bootstrapped->verify_proof_to_peak(maximum - 1, leaf, {}));
    REQUIRE(bootstrapped->verify_proof_to_root(maximum - 1, leaf, last_branch));
    auto const snapshot = *bootstrapped;
    REQUIRE_FALSE(bootstrapped->extend(leaf));
    REQUIRE(*bootstrapped == snapshot);
}

TEST_CASE("mmr API uses internal hash bytes and accepts zero hashes as leaves", "[shv]") {
    auto const& vector = kth::test::shv::proofs[0];
    auto const raw = decode_base16(vector.header);
    REQUIRE(raw);
    auto const internal = bitcoin_hash(*raw);
    auto const display_bytes = decode_base16<hash_size>(kth::test::shv::first_16[0]);
    REQUIRE(display_bytes);
    REQUIRE(internal != *display_bytes);
    auto reversed = *display_bytes;
    std::reverse(reversed.begin(), reversed.end());
    REQUIRE(internal == reversed);
    REQUIRE(encode_hash(internal) == kth::test::shv::first_16[0]);
    REQUIRE(mmr_node_hash(internal, reversed) == bitcoin_hash(splice(internal, reversed)));
    auto const branch = display_hashes(vector.branch);
    auto const expected = display_hash(vector.root);
    REQUIRE(verify_mmr_proof(0, 11, internal, branch, expected));
    REQUIRE_FALSE(verify_mmr_proof(0, 11, *display_bytes, branch, expected));
    hash_list wrong_branch;
    for (auto const* text : vector.branch) {
        auto const bytes = decode_base16<hash_size>(text);
        REQUIRE(bytes);
        wrong_branch.push_back(*bytes);
    }
    REQUIRE_FALSE(verify_mmr_proof(0, 11, internal, wrong_branch, expected));

    auto const server = build_mmr({null_hash, null_hash});
    auto const client = build_accumulator({null_hash, null_hash});
    auto const proof = server.proof_to_peak(1, 2, null_hash);
    REQUIRE(proof);
    REQUIRE(client.verify_proof_to_peak(1, null_hash, *proof));
    REQUIRE(server.root(2) == client.root());
    // Trusted peaks disambiguate equal real leaves; root-only paths cannot.
    REQUIRE_FALSE(client.verify_proof_to_root(1, null_hash, *proof));
    auto const single = mmr_accumulator::bootstrap(1, null_hash, {}, null_hash);
    REQUIRE(single);
    REQUIRE(single->verify_proof_to_root(0, null_hash, {}));
    REQUIRE(single->verify_proof_to_peak(0, null_hash, {}));
}
