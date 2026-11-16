// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Signature hashes of every input of a transaction, with the hashes of the
// whole transaction computed for each signature and with them kept for the
// transaction (chain::sighash_cache), and what that is worth in a whole
// verification.

#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>

#include <cstdint>
#include <string>
#include <vector>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/sighash_cache.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>

using namespace kth;
using namespace kth::domain;
using namespace kth::domain::machine;
using namespace kth::infrastructure::machine;
using ankerl::nanobench::Bench;

namespace {

constexpr script_flags_t flags = to_flags(upgrade::bch_leibniz);
constexpr uint8_t sighash_type = uint8_t(sighash_algorithm::all | sighash_algorithm::forkid);
constexpr size_t output_count = 2;

ec_secret secret() {
    ec_secret out{};
    out.fill(0x11);
    return out;
}

// Pay to the public key of secret().
chain::script prevout_script() {
    ec_compressed point;
    if ( ! secret_to_public(point, secret())) std::abort();
    return chain::script(operation::list{operation(data_chunk(point.begin(), point.end())), operation(opcode::checksig)});
}

chain::transaction make_transaction(size_t inputs, std::vector<chain::script> const& input_scripts = {}) {
    auto const locking = prevout_script();
    chain::input::list ins;
    for (size_t i = 0; i < inputs; ++i) {
        hash_digest previous{};
        previous[0] = uint8_t(i);
        previous[1] = uint8_t(i >> 8);
        previous[31] = 0xab;
        ins.emplace_back(chain::output_point{previous, uint32_t(i)},
            i < input_scripts.size() ? input_scripts[i] : chain::script{}, 0xfffffffeu);
    }
    chain::output::list outs;
    for (size_t i = 0; i < output_count; ++i) {
        outs.emplace_back(uint64_t(1000 * (i + 1)), locking, std::nullopt);
    }
    chain::transaction tx(2, 0, std::move(ins), std::move(outs));
    for (size_t i = 0; i < inputs; ++i) {
        tx.inputs()[i].previous_output().validation.cache = chain::output(uint64_t(5000 + i), locking, std::nullopt);
    }
    return tx;
}

chain::transaction make_signed_transaction(size_t inputs) {
    auto const unsigned_tx = make_transaction(inputs);
    auto const locking = prevout_script();
    std::vector<chain::script> signatures;
    for (uint32_t i = 0; i < inputs; ++i) {
        auto const endorsement = chain::script::create_endorsement(secret(), locking, unsigned_tx, i, sighash_type, flags,
            uint64_t(5000 + i));
        if ( ! endorsement) std::abort();
        signatures.emplace_back(operation::list{operation(*endorsement)});
    }
    return make_transaction(inputs, signatures);
}

void measure(size_t inputs) {
    auto const tx = make_signed_transaction(inputs);
    auto const locking = prevout_script();
    auto const name = [&](char const* what) { return std::string(what) + ", " + std::to_string(inputs) + " inputs"; };

    Bench bench;
    bench.title("signature hashes of a transaction").unit("input").batch(inputs)
        .minEpochIterations(inputs >= 1000 ? 3 : 200);

    bench.run(name("signature hash, computed for each input"), [&] {
        for (uint32_t i = 0; i < inputs; ++i) {
            ankerl::nanobench::doNotOptimizeAway(
                chain::script::generate_signature_hash(tx, i, locking, sighash_type, flags, uint64_t(5000 + i)));
        }
    });
    bench.run(name("signature hash, shared hashes kept"), [&] {
        chain::sighash_cache shared(tx);
        for (uint32_t i = 0; i < inputs; ++i) {
            ankerl::nanobench::doNotOptimizeAway(
                chain::script::generate_signature_hash(tx, i, locking, sighash_type, flags, uint64_t(5000 + i), &shared));
        }
    });
    bench.run(name("verify every input, computed for each input"), [&] {
        for (uint32_t i = 0; i < inputs; ++i) {
            if (chain::verify_with_sigchecks(tx, i, flags).first != error::success) std::abort();
        }
    });
    bench.run(name("verify every input, shared hashes kept"), [&] {
        chain::sighash_cache shared(tx);
        for (uint32_t i = 0; i < inputs; ++i) {
            if (chain::verify_with_sigchecks(tx, i, flags, &shared).first != error::success) std::abort();
        }
    });
}

} // namespace

int main() {
    for (size_t inputs : {1, 2, 10, 100, 1000}) measure(inputs);
}
