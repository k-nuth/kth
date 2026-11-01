// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests of the BCHN standardness policy (kth::domain::policy::*_bchn), ported from the behaviour of
// Bitcoin Cash Node (commit 07576013c91ff4a3a74acd85f189c69121cdad1b):
//   src/policy/policy.cpp (IsStandardTx, AreInputsStandard, GetDustThreshold, IsDust,
//   GetVirtualTransactionSize), src/script/standard.cpp (Solver),
//   src/test/transaction_tests.cpp (test_IsStandard), src/test/script_standard_tests.cpp,
//   src/test/multisig_tests.cpp.
// Original: Copyright (c) 2017 The Bitcoin Core developers,
//           Copyright (c) 2019-2026 The Bitcoin developers, MIT license.
//
// The expected values are computed by hand from the BCHN sources, not taken from the implementation.

#if defined(KTH_CURRENCY_BCH)
#include <test_helpers.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include <kth/domain/policy/standardness_bchn.hpp>

using namespace kth;
namespace chain = kth::domain::chain;
namespace policy = kth::domain::policy;
using kth::domain::machine::script_flags;
using kth::domain::script_flags_t;
using policy::bchn_script_type;

namespace {

constexpr uint8_t OP_0 = 0x00, OP_PUSHDATA1 = 0x4c, OP_PUSHDATA2 = 0x4d, OP_PUSHDATA4 = 0x4e;
constexpr uint8_t OP_RESERVED = 0x50, OP_1 = 0x51, OP_2 = 0x52, OP_3 = 0x53, OP_4 = 0x54, OP_16 = 0x60;
constexpr uint8_t OP_RETURN = 0x6a, OP_DROP = 0x75, OP_DUP = 0x76, OP_EQUAL = 0x87, OP_EQUALVERIFY = 0x88;
constexpr uint8_t OP_ADD = 0x93, OP_HASH160 = 0xa9, OP_HASH256 = 0xaa, OP_CHECKSIG = 0xac, OP_CHECKMULTISIG = 0xae;
constexpr uint8_t OP_NOP = 0x61;

// Raw script bytes, so that the exact push opcode of each element is chosen by the test.
struct raw {
    data_chunk bytes;

    raw& op(uint8_t opcode) { bytes.push_back(opcode); return *this; }

    // Minimal push of `data` (what BCHN's `CScript << vector` produces)
    raw& push(data_chunk const& data) {
        if (data.size() < 76) {
            bytes.push_back(uint8_t(data.size()));
        } else if (data.size() <= 0xff) {
            bytes.push_back(OP_PUSHDATA1);
            bytes.push_back(uint8_t(data.size()));
        } else {
            bytes.push_back(OP_PUSHDATA2);
            bytes.push_back(uint8_t(data.size()));
            bytes.push_back(uint8_t(data.size() >> 8));
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }

    // Push of `data` with a specific PUSHDATA opcode
    raw& push_with(uint8_t pushdata_opcode, data_chunk const& data) {
        bytes.push_back(pushdata_opcode);
        auto const size = data.size();
        bytes.push_back(uint8_t(size));
        if (pushdata_opcode != OP_PUSHDATA1) {
            bytes.push_back(uint8_t(size >> 8));
        }
        if (pushdata_opcode == OP_PUSHDATA4) {
            bytes.push_back(uint8_t(size >> 16));
            bytes.push_back(uint8_t(size >> 24));
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }

    chain::script script() const {
        byte_reader reader(bytes);
        auto result = chain::script::from_data(reader, false);
        REQUIRE(result);
        return std::move(*result);
    }
};

data_chunk compressed_key(uint8_t seed, uint8_t header = 0x02) {
    data_chunk key(33, seed);
    key[0] = header;
    return key;
}

data_chunk uncompressed_key(uint8_t seed, uint8_t header = 0x04) {
    data_chunk key(65, seed);
    key[0] = header;
    return key;
}

data_chunk filler(size_t size, uint8_t seed = 1) { return data_chunk(size, seed); }

// Pre upgrade 12 / pre tokens policy flags and the ones after them.
constexpr script_flags_t p2sh = script_flags::bip16_rule;
constexpr script_flags_t before_tokens = p2sh;
constexpr script_flags_t descartes = p2sh | script_flags::bch_p2sh_32 | script_flags::bch_tokens;
constexpr script_flags_t upgrade_12 = descartes | script_flags::bch_p2s;

chain::script p2pkh_script() {
    return raw{}.op(OP_DUP).op(OP_HASH160).push(filler(20, 0x44)).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script();
}

chain::script p2sh_script() { return raw{}.op(OP_HASH160).push(filler(20, 0x55)).op(OP_EQUAL).script(); }

chain::script p2sh32_script() { return raw{}.op(OP_HASH256).push(filler(32, 0x66)).op(OP_EQUAL).script(); }

chain::script p2pk_script() { return raw{}.push(compressed_key(0x11)).op(OP_CHECKSIG).script(); }

// OP_RETURN script of exactly `size` bytes: OP_RETURN, then pushes (PUSHDATA1 up to 255 bytes of payload).
chain::script null_data_script(size_t size) {
    REQUIRE(size >= 1);
    raw r;
    r.op(OP_RETURN);
    if (size == 1) {
        return r.script();
    }
    if (size == 2) {
        r.op(OP_0);
        return r.script();
    }
    if (size <= 77) {
        r.push(filler(size - 2));      // 1 push byte + payload
        return r.script();
    }
    REQUIRE(size != 78);               // no single push gives 78 bytes
    r.push(filler(size - 3));          // OP_PUSHDATA1 + length + payload
    return r.script();
}

// Any 201-byte-or-less script that is not a template: OP_NOP + padding.
chain::script generic_script(size_t size) {
    raw r;
    r.bytes.assign(size, OP_NOP);
    return r.script();
}

chain::output out(uint64_t value, chain::script const& script) { return chain::output(value, script, std::nullopt); }

chain::output token_out(uint64_t value, chain::script const& script) {
    return chain::output(value, script, chain::make_fungible(hash_digest{{7}}, 1));
}

hash_digest hash_of(uint8_t seed) { return hash_digest{{seed}}; }

// Scriptsig of `size` bytes which is push only: a single PUSHDATA2 push.
chain::script push_only_script(size_t size) {
    REQUIRE(size >= 3);
    return raw{}.push_with(OP_PUSHDATA2, filler(size - 3)).script();
}

chain::input in(chain::script const& script_sig = raw{}.push(filler(10)).script(), uint8_t seed = 1) {
    return chain::input(chain::output_point(hash_of(seed), 0), script_sig, 0xffffffff);
}

chain::transaction make_tx(std::vector<chain::input> inputs, std::vector<chain::output> outputs, uint32_t version = 2) {
    return chain::transaction(version, 0, std::move(inputs), std::move(outputs));
}

// A standard transaction with a single P2PKH output of `value`.
chain::transaction simple_tx(uint64_t value = 100'000) {
    return make_tx({in()}, {out(value, p2pkh_script())});
}

// Transaction of exactly `target` bytes, padded with push only scriptSigs.
chain::transaction tx_of_size(size_t target) {
    constexpr size_t filler_input_size = 36 + 3 + 1000 + 4;
    std::vector<chain::input> inputs;
    auto const output = out(100'000, p2pkh_script());
    // version + input count (1 byte) + output count (1 byte) + output + locktime
    size_t remaining = target - (4 + 1 + 1 + output.serialized_size(true) + 4);
    // The last input takes what is left, 296 or more bytes (a scriptSig of 253 or more has a 3 bytes length)
    while (remaining >= filler_input_size + 296) {
        inputs.push_back(in(push_only_script(1000), uint8_t(inputs.size() + 1)));
        remaining -= filler_input_size;
    }
    inputs.push_back(in(push_only_script(remaining - (36 + 3 + 4)), uint8_t(inputs.size() + 1)));
    return make_tx(std::move(inputs), {output}, 1);
}

chain::transaction spend_with_prevouts(std::vector<chain::output> prevouts) {
    std::vector<chain::input> inputs;
    uint8_t seed = 1;
    for (auto const& prevout : prevouts) {
        inputs.push_back(in(raw{}.push(filler(10)).script(), seed++));
        inputs.back().previous_output().validation.cache = prevout;   // chain::output{} stays "not found"
    }
    return make_tx(std::move(inputs), {out(1000, p2pkh_script())});
}

bool is_error(code const& ec, error::error_code_t expected) { return ec == expected; }

} // namespace

// Solver
//-----------------------------------------------------------------------------

TEST_CASE("BCHN standardness - Solver templates", "[bchn_non_vm][standardness_bchn]") {
    auto const type = [](chain::script const& s, script_flags_t flags = descartes) { return policy::solve_script_bchn(s, flags).type; };

    SECTION("P2PK compressed and uncompressed with valid header bytes") {
        for (uint8_t header : {uint8_t(2), uint8_t(3)}) {
            CHECK(type(raw{}.push(compressed_key(0x11, header)).op(OP_CHECKSIG).script()) == bchn_script_type::pubkey);
        }
        for (uint8_t header : {uint8_t(4), uint8_t(6), uint8_t(7)}) {
            CHECK(type(raw{}.push(uncompressed_key(0x11, header)).op(OP_CHECKSIG).script()) == bchn_script_type::pubkey);
        }
    }

    SECTION("P2PK with a header byte that does not match the size is not a template (CPubKey::ValidSize)") {
        CHECK(type(raw{}.push(compressed_key(0x11, 0x05)).op(OP_CHECKSIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.push(compressed_key(0x11, 0x04)).op(OP_CHECKSIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.push(uncompressed_key(0x11, 0x02)).op(OP_CHECKSIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.push(uncompressed_key(0x11, 0x05)).op(OP_CHECKSIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.push(compressed_key(0x11, 0x00)).op(OP_CHECKSIG).script()) == bchn_script_type::nonstandard);
    }

    SECTION("P2PK with a non minimal push is not a template (raw bytes)") {
        for (uint8_t pushdata : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
            auto const s = raw{}.push_with(pushdata, compressed_key(0x11)).op(OP_CHECKSIG).script();
            CHECK(type(s) == bchn_script_type::nonstandard);
            CHECK(type(s, upgrade_12) == bchn_script_type::script);
        }
    }

    SECTION("P2PKH needs a 20 bytes direct push") {
        CHECK(type(p2pkh_script()) == bchn_script_type::pubkeyhash);
        for (uint8_t pushdata : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
            auto const s = raw{}.op(OP_DUP).op(OP_HASH160).push_with(pushdata, filler(20)).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script();
            CHECK(type(s) == bchn_script_type::nonstandard);
            CHECK(type(s, upgrade_12) == bchn_script_type::script);
        }
        CHECK(type(raw{}.op(OP_DUP).op(OP_HASH160).push(filler(21)).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script()) == bchn_script_type::nonstandard);
    }

    SECTION("P2SH 20 always, P2SH 32 only with the p2sh_32 flag") {
        CHECK(type(p2sh_script(), before_tokens) == bchn_script_type::scripthash);
        CHECK(type(p2sh_script(), descartes) == bchn_script_type::scripthash);
        CHECK(type(p2sh32_script(), before_tokens) == bchn_script_type::nonstandard);
        CHECK(type(p2sh32_script(), p2sh | script_flags::bch_p2s) == bchn_script_type::script);
        CHECK(type(p2sh32_script(), descartes) == bchn_script_type::scripthash);
        CHECK(type(p2sh32_script(), upgrade_12) == bchn_script_type::scripthash);
        // The p2sh_32 template is not gated by bip16, the 20 bytes one is not either: BCHN checks only the flag
        CHECK(type(p2sh_script(), 0) == bchn_script_type::scripthash);
        CHECK(type(p2sh32_script(), script_flags::bch_p2sh_32) == bchn_script_type::scripthash);
    }

    SECTION("P2SH with non minimal push is not a template") {
        for (uint8_t pushdata : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
            CHECK(type(raw{}.op(OP_HASH160).push_with(pushdata, filler(20)).op(OP_EQUAL).script()) == bchn_script_type::nonstandard);
            CHECK(type(raw{}.op(OP_HASH256).push_with(pushdata, filler(32)).op(OP_EQUAL).script()) == bchn_script_type::nonstandard);
        }
    }

    SECTION("OP_RETURN with any push only tail, minimal or not, is null data") {
        CHECK(type(raw{}.op(OP_RETURN).script()) == bchn_script_type::null_data);
        CHECK(type(raw{}.op(OP_RETURN).push(filler(10)).script()) == bchn_script_type::null_data);
        CHECK(type(raw{}.op(OP_RETURN).push(filler(10)).push(filler(20)).script()) == bchn_script_type::null_data);
        for (uint8_t pushdata : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
            CHECK(type(raw{}.op(OP_RETURN).push_with(pushdata, filler(3)).script()) == bchn_script_type::null_data);
        }
        // OP_RESERVED and OP_1..OP_16 are push-type opcodes
        CHECK(type(raw{}.op(OP_RETURN).op(OP_RESERVED).op(OP_16).op(OP_0).script()) == bchn_script_type::null_data);
        raw mixed;
        mixed.bytes = {OP_RETURN, OP_RESERVED, OP_PUSHDATA1, 0x00, 0x01, 0x01, OP_PUSHDATA4, 0x01, 0x00, 0x00, 0x00, 0xaa};
        CHECK(type(mixed.script()) == bchn_script_type::null_data);
    }

    SECTION("OP_RETURN with a non push opcode, or a truncated push, is not null data") {
        CHECK(type(raw{}.op(OP_RETURN).push({75}).op(OP_ADD).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.op(OP_RETURN).push({75}).op(OP_ADD).script(), upgrade_12) == bchn_script_type::script);
        raw truncated;
        truncated.bytes = {OP_RETURN, 0x05, 0x01, 0x02};
        CHECK(type(truncated.script()) == bchn_script_type::nonstandard);
        CHECK(type(truncated.script(), upgrade_12) == bchn_script_type::script);
        raw truncated_pushdata;
        truncated_pushdata.bytes = {OP_RETURN, OP_PUSHDATA2, 0x01};
        CHECK(type(truncated_pushdata.script()) == bchn_script_type::nonstandard);
    }

    SECTION("Multisig of 1 to 16 keys is a template, with its m and n") {
        data_chunk const k[4] = {compressed_key(1), compressed_key(2), uncompressed_key(3), compressed_key(4, 0x03)};
        auto const s = raw{}.op(OP_2).push(k[0]).push(k[1]).push(k[2]).op(OP_3).op(OP_CHECKMULTISIG).script();
        auto const solution = policy::solve_script_bchn(s, descartes);
        CHECK(solution.type == bchn_script_type::multisig);
        CHECK(solution.required == 2);
        CHECK(solution.keys == 3);
        auto const s4 = raw{}.op(OP_1).push(k[0]).push(k[1]).push(k[2]).push(k[3]).op(OP_4).op(OP_CHECKMULTISIG).script();
        auto const solution4 = policy::solve_script_bchn(s4, upgrade_12);
        CHECK(solution4.type == bchn_script_type::multisig);
        CHECK(solution4.required == 1);
        CHECK(solution4.keys == 4);
    }

    SECTION("Malformed multisig is not a template") {
        auto const k0 = compressed_key(1), k1 = compressed_key(2);
        CHECK(type(raw{}.op(OP_0).push(k0).op(OP_1).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);       // 0 of 1
        CHECK(type(raw{}.op(OP_2).push(k0).op(OP_1).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);       // 2 of 1
        CHECK(type(raw{}.op(OP_1).push(k0).op(OP_2).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);       // n = 2, 1 key
        CHECK(type(raw{}.op(OP_1).push(k0).push(k1).op(OP_1).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard); // n = 1, 2 keys
        CHECK(type(raw{}.op(OP_1).op(OP_1).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);                // no keys
        CHECK(type(raw{}.op(OP_1).push(k0).push(k1).op(OP_0).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.op(OP_1).push(k0).push(k1).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.op(OP_1).push(k0).push(k1).script()) == bchn_script_type::nonstandard);
        // a key with a bad header byte ends the key list
        CHECK(type(raw{}.op(OP_1).push(compressed_key(1, 0x05)).op(OP_1).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        // trailing junk before OP_CHECKMULTISIG
        CHECK(type(raw{}.op(OP_1).push(k0).op(OP_1).op(OP_NOP).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        // m or n as a push, not OP_N
        CHECK(type(raw{}.push({1}).push(k0).push(k1).op(OP_2).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.op(OP_1).push(k0).push(k1).push({2}).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        for (uint8_t pushdata : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
            CHECK(type(raw{}.push_with(pushdata, {1}).push(k0).push(k1).op(OP_2).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
            // non minimal key push
            CHECK(type(raw{}.op(OP_1).push_with(pushdata, k0).push(k1).op(OP_2).op(OP_CHECKMULTISIG).script()) == bchn_script_type::nonstandard);
        }
    }

    SECTION("Pay to script: any other script of up to 201 bytes, only with the p2s flag") {
        for (size_t size : {size_t(1), size_t(100), size_t(200), size_t(201)}) {
            DYNAMIC_SECTION("size " << size) {
                CHECK(type(generic_script(size), descartes) == bchn_script_type::nonstandard);
                CHECK(type(generic_script(size), upgrade_12) == bchn_script_type::script);
            }
        }
        CHECK(type(generic_script(202), upgrade_12) == bchn_script_type::nonstandard);
        CHECK(type(generic_script(10'000), upgrade_12) == bchn_script_type::nonstandard);
        // The empty script is 0 bytes, <= 201
        CHECK(type(chain::script{}, descartes) == bchn_script_type::nonstandard);
        CHECK(type(chain::script{}, upgrade_12) == bchn_script_type::script);
        // OP_0 <20 bytes>, witness-like programs are plain scripts on BCH
        CHECK(type(raw{}.op(OP_0).push(filler(20)).script(), descartes) == bchn_script_type::nonstandard);
        CHECK(type(raw{}.op(OP_0).push(filler(20)).script(), upgrade_12) == bchn_script_type::script);
    }

    SECTION("Templates do not depend on p2s") {
        CHECK(type(p2pk_script(), upgrade_12) == bchn_script_type::pubkey);
        CHECK(type(p2pkh_script(), upgrade_12) == bchn_script_type::pubkeyhash);
        CHECK(type(p2sh_script(), upgrade_12) == bchn_script_type::scripthash);
    }
}

// IsStandard(scriptPubKey)
//-----------------------------------------------------------------------------

TEST_CASE("BCHN standardness - check_script_standardness", "[bchn_non_vm][standardness_bchn]") {
    auto const ok = [](chain::script const& s, script_flags_t flags) { return policy::check_script_standardness_bchn(s, flags) == error::success; };
    auto const bad = [](chain::script const& s, script_flags_t flags) {
        return is_error(policy::check_script_standardness_bchn(s, flags), error::standardness_scriptpubkey);
    };

    data_chunk const k[4] = {compressed_key(1), compressed_key(2), compressed_key(3), compressed_key(4)};
    CHECK(ok(raw{}.op(OP_1).push(k[0]).op(OP_1).op(OP_CHECKMULTISIG).script(), descartes));
    CHECK(ok(raw{}.op(OP_2).push(k[0]).push(k[1]).op(OP_2).op(OP_CHECKMULTISIG).script(), descartes));
    CHECK(ok(raw{}.op(OP_3).push(k[0]).push(k[1]).push(k[2]).op(OP_3).op(OP_CHECKMULTISIG).script(), descartes));

    // 1-of-4 is a multisig template and is not standard, also with p2s (it is not a plain script)
    auto const one_of_four = raw{}.op(OP_1).push(k[0]).push(k[1]).push(k[2]).push(k[3]).op(OP_4).op(OP_CHECKMULTISIG).script();
    REQUIRE(one_of_four.serialized_size(false) <= policy::bchn_max_p2s_script_size);
    CHECK(bad(one_of_four, descartes));
    CHECK(bad(one_of_four, upgrade_12));

    CHECK(ok(p2pkh_script(), before_tokens));
    CHECK(ok(null_data_script(10), before_tokens));
    CHECK(bad(generic_script(10), descartes));
    CHECK(ok(generic_script(10), upgrade_12));
    CHECK(bad(generic_script(202), upgrade_12));
}

// IsStandardTx
//-----------------------------------------------------------------------------

TEST_CASE("BCHN standardness - transaction baseline", "[bchn_non_vm][standardness_bchn]") {
    CHECK(policy::check_transaction_standardness_bchn(simple_tx(), descartes) == error::success);
    CHECK(policy::check_transaction_standardness_bchn(simple_tx(), upgrade_12) == error::success);
    CHECK(policy::check_transaction_standardness_bchn(simple_tx(), before_tokens) == error::success);
}

TEST_CASE("BCHN standardness - version", "[bchn_non_vm][standardness_bchn]") {
    for (uint32_t version : {1u, 2u}) {
        CHECK(policy::check_transaction_standardness_bchn(make_tx({in()}, {out(100'000, p2pkh_script())}, version), descartes) == error::success);
    }
    for (uint32_t version : {0u, 3u, 4u, 0xffffffffu, 0x80000000u}) {
        DYNAMIC_SECTION("version " << version) {
            CHECK(is_error(policy::check_transaction_standardness_bchn(make_tx({in()}, {out(100'000, p2pkh_script())}, version), descartes),
                           error::standardness_version));
        }
    }
}

TEST_CASE("BCHN standardness - transaction size", "[bchn_non_vm][standardness_bchn]") {
    auto const at_limit = tx_of_size(policy::bchn_max_standard_tx_size);
    REQUIRE(at_limit.serialized_size(true) == 100'000);
    CHECK(policy::check_transaction_standardness_bchn(at_limit, descartes) == error::success);

    auto const over = tx_of_size(policy::bchn_max_standard_tx_size + 1);
    REQUIRE(over.serialized_size(true) == 100'001);
    CHECK(is_error(policy::check_transaction_standardness_bchn(over, descartes), error::standardness_transaction_size));

    auto const below = tx_of_size(policy::bchn_max_standard_tx_size - 1);
    REQUIRE(below.serialized_size(true) == 99'999);
    CHECK(policy::check_transaction_standardness_bchn(below, descartes) == error::success);
    // the limit does not depend on the upgrade
    CHECK(is_error(policy::check_transaction_standardness_bchn(over, upgrade_12), error::standardness_transaction_size));
}

TEST_CASE("BCHN standardness - scriptSig size and push only", "[bchn_non_vm][standardness_bchn]") {
    auto const tx_with_script_sig = [](chain::script const& s) { return make_tx({in(s)}, {out(100'000, p2pkh_script())}); };

    SECTION("before upgrade 12 the limit is 1650 bytes") {
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(push_only_script(1650)), descartes) == error::success);
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(push_only_script(1651)), descartes),
                       error::standardness_scriptsig_size));
    }

    SECTION("after upgrade 12 the limit is the maximum script size") {
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(push_only_script(1651)), upgrade_12) == error::success);
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(push_only_script(9999)), upgrade_12) == error::success);
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(push_only_script(10'000)), upgrade_12) == error::success);
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(push_only_script(10'001)), upgrade_12),
                       error::standardness_scriptsig_size));
    }

    SECTION("the size is checked before push only, per input") {
        raw not_push_only;
        not_push_only.bytes.assign(1651, OP_NOP);
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(not_push_only.script()), descartes),
                       error::standardness_scriptsig_size));
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(not_push_only.script()), upgrade_12),
                       error::standardness_scriptsig_not_push_only));
        // an input that is not push only wins over an oversized scriptSig of the next input
        auto const tx = make_tx({in(raw{}.op(OP_NOP).script(), 1), in(push_only_script(1651), 2)}, {out(100'000, p2pkh_script())});
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx, descartes), error::standardness_scriptsig_not_push_only));
    }

    SECTION("push only: OP_0, OP_1NEGATE, OP_RESERVED, OP_1..OP_16 and pushes are push; anything else is not") {
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(raw{}.op(OP_0).op(0x4f).op(OP_RESERVED).op(OP_1).op(OP_16).script()), descartes) == error::success);
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(chain::script{}), descartes) == error::success);
        CHECK(policy::check_transaction_standardness_bchn(tx_with_script_sig(raw{}.push(filler(10)).push_with(OP_PUSHDATA4, filler(5)).script()), descartes) == error::success);
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(raw{}.op(0x61).script()), descartes),
                       error::standardness_scriptsig_not_push_only));                       // OP_NOP, 0x61 > OP_16
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(raw{}.push(filler(10)).op(OP_DROP).script()), descartes),
                       error::standardness_scriptsig_not_push_only));
        raw truncated;
        truncated.bytes = {0x05, 0x01};
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx_with_script_sig(truncated.script()), descartes),
                       error::standardness_scriptsig_not_push_only));
    }
}

TEST_CASE("BCHN standardness - tokens before activation", "[bchn_non_vm][standardness_bchn]") {
    auto const tx = make_tx({in()}, {token_out(100'000, p2pkh_script())});
    CHECK(is_error(policy::check_transaction_standardness_bchn(tx, before_tokens), error::standardness_tokens_before_activation));
    CHECK(policy::check_transaction_standardness_bchn(tx, descartes) == error::success);

    // reported before the script check of the same output, and before the dust of earlier outputs is not an issue (dust comes later)
    auto const nonstandard = make_tx({in()}, {token_out(100'000, generic_script(10))});
    CHECK(is_error(policy::check_transaction_standardness_bchn(nonstandard, before_tokens), error::standardness_tokens_before_activation));
    CHECK(is_error(policy::check_transaction_standardness_bchn(nonstandard, descartes), error::standardness_scriptpubkey));
    // an earlier nonstandard output is reported first
    auto const earlier = make_tx({in()}, {out(100'000, generic_script(10)), token_out(100'000, p2pkh_script())});
    CHECK(is_error(policy::check_transaction_standardness_bchn(earlier, before_tokens), error::standardness_scriptpubkey));
}

TEST_CASE("BCHN standardness - output templates", "[bchn_non_vm][standardness_bchn]") {
    auto const check = [](chain::script const& s, script_flags_t flags) {
        return policy::check_transaction_standardness_bchn(make_tx({in()}, {out(100'000, s)}), flags);
    };

    CHECK(check(p2pk_script(), descartes) == error::success);
    CHECK(check(p2pkh_script(), descartes) == error::success);
    CHECK(check(p2sh_script(), descartes) == error::success);
    CHECK(check(p2sh32_script(), descartes) == error::success);
    CHECK(is_error(check(p2sh32_script(), before_tokens), error::standardness_scriptpubkey));
    CHECK(is_error(check(generic_script(50), descartes), error::standardness_scriptpubkey));
    CHECK(check(generic_script(50), upgrade_12) == error::success);
    CHECK(check(generic_script(201), upgrade_12) == error::success);
    CHECK(is_error(check(generic_script(202), upgrade_12), error::standardness_scriptpubkey));
}

TEST_CASE("BCHN standardness - bare multisig", "[bchn_non_vm][standardness_bchn]") {
    data_chunk const k[4] = {compressed_key(1), compressed_key(2), compressed_key(3), compressed_key(4)};
    auto const one_of_two = raw{}.op(OP_1).push(k[0]).push(k[1]).op(OP_2).op(OP_CHECKMULTISIG).script();
    auto const check = [&](chain::script const& s, uint64_t value, policy::bchn_standardness_parameters const& p = {}) {
        return policy::check_transaction_standardness_bchn(make_tx({in()}, {out(value, s)}), descartes, p);
    };

    CHECK(check(one_of_two, 100'000) == error::success);

    policy::bchn_standardness_parameters no_bare;
    no_bare.permit_bare_multisig = false;
    CHECK(is_error(check(one_of_two, 100'000, no_bare), error::standardness_bare_multisig));
    // bare multisig is rejected before the dust is checked
    CHECK(is_error(check(one_of_two, 1, no_bare), error::standardness_bare_multisig));
    // other templates do not care about the flag
    CHECK(policy::check_transaction_standardness_bchn(simple_tx(), descartes, no_bare) == error::success);

    // 1-of-2: 8 + 1 + 71 = 80 bytes + 148 = 228 -> 684 satoshis
    CHECK(is_error(check(one_of_two, 683), error::standardness_dust));
    CHECK(check(one_of_two, 684) == error::success);

    // more than 3 keys, also after upgrade 12
    auto const one_of_four = raw{}.op(OP_1).push(k[0]).push(k[1]).push(k[2]).push(k[3]).op(OP_4).op(OP_CHECKMULTISIG).script();
    CHECK(is_error(check(one_of_four, 100'000), error::standardness_scriptpubkey));
    CHECK(is_error(policy::check_transaction_standardness_bchn(make_tx({in()}, {out(100'000, one_of_four)}), upgrade_12),
                   error::standardness_scriptpubkey));
}

TEST_CASE("BCHN standardness - dust", "[bchn_non_vm][standardness_bchn]") {
    auto const check = [](uint64_t value, chain::script const& s, policy::bchn_standardness_parameters const& p = {}, script_flags_t flags = descartes) {
        return policy::check_transaction_standardness_bchn(make_tx({in()}, {out(value, s)}), flags, p);
    };

    SECTION("P2PKH: 8 + 1 + 25 + 148 = 182 bytes, 3 * 182 = 546 satoshis") {
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script())) == 546);
        CHECK(is_error(check(545, p2pkh_script()), error::standardness_dust));
        CHECK(check(546, p2pkh_script()) == error::success);
        CHECK(policy::is_dust_bchn(out(545, p2pkh_script())));
        CHECK( ! policy::is_dust_bchn(out(546, p2pkh_script())));
    }

    SECTION("other templates") {
        CHECK(policy::get_dust_threshold_bchn(out(0, p2sh_script())) == 3 * (8 + 1 + 23 + 148));         // 540
        CHECK(policy::get_dust_threshold_bchn(out(0, p2sh32_script())) == 3 * (8 + 1 + 35 + 148));       // 576
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pk_script())) == 3 * (8 + 1 + 35 + 148));         // 576
    }

    SECTION("pay to script of 201 bytes") {
        CHECK(policy::get_dust_threshold_bchn(out(0, generic_script(201))) == 3 * (8 + 1 + 201 + 148));  // 1074
        CHECK(is_error(check(1073, generic_script(201), {}, upgrade_12), error::standardness_dust));
        CHECK(check(1074, generic_script(201), {}, upgrade_12) == error::success);
    }

    SECTION("a token adds its serialization to the output") {
        // fungible token of amount 1: 1 prefix + 32 id + 1 bitfield + 1 amount = 35, plus the 25 bytes of script
        auto const token = token_out(0, p2pkh_script());
        REQUIRE(token.serialized_size(true) == 8 + 1 + (35 + 25));
        CHECK(policy::get_dust_threshold_bchn(token) == 3 * (token.serialized_size(true) + 148));
        CHECK(policy::get_dust_threshold_bchn(token) == 651);
        auto const tx = [&](uint64_t value) { return make_tx({in()}, {token_out(value, p2pkh_script())}); };
        CHECK(is_error(policy::check_transaction_standardness_bchn(tx(650), descartes), error::standardness_dust));
        CHECK(policy::check_transaction_standardness_bchn(tx(651), descartes) == error::success);
    }

    SECTION("unspendable outputs have no dust threshold") {
        CHECK(policy::get_dust_threshold_bchn(out(0, null_data_script(40))) == 0);
        CHECK( ! policy::is_dust_bchn(out(0, null_data_script(40))));
        // a script bigger than the maximum script size is unspendable, one of exactly that size is not
        CHECK(policy::get_dust_threshold_bchn(out(0, generic_script(10'001))) == 0);
        CHECK(policy::get_dust_threshold_bchn(out(0, generic_script(10'000))) == 3 * (8 + 3 + 10'000 + 148));
        // an OP_RETURN output is not checked for dust in a transaction either
        CHECK(policy::check_transaction_standardness_bchn(make_tx({in()}, {out(0, null_data_script(40))}), descartes) == error::success);
    }

    SECTION("dust relay fee: rounding down, at least 1 satoshi per transaction when positive") {
        // 182 * 1234 / 1000 = 224.588 -> 224, times 3
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 1234) == 672);
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 1000) == 546);
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 2000) == 1092);
        // 182 * 5 / 1000 = 0.91 -> 0 -> raised to 1, times 3
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 5) == 3);
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 1) == 3);
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 0) == 0);
        // 182 * 6 / 1000 = 1.09 -> 1
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 6) == 3);
        // 182 * 11 / 1000 = 2.002 -> 2
        CHECK(policy::get_dust_threshold_bchn(out(0, p2pkh_script()), 11) == 6);

        policy::bchn_standardness_parameters fee_1234;
        fee_1234.dust_relay_fee_per_kb = 1234;
        CHECK(is_error(check(671, p2pkh_script(), fee_1234), error::standardness_dust));
        CHECK(check(672, p2pkh_script(), fee_1234) == error::success);
        policy::bchn_standardness_parameters no_dust;
        no_dust.dust_relay_fee_per_kb = 0;
        CHECK(check(0, p2pkh_script(), no_dust) == error::success);
    }
}

TEST_CASE("BCHN standardness - OP_RETURN data carrier", "[bchn_non_vm][standardness_bchn]") {
    auto const check = [](std::vector<chain::output> outputs, policy::bchn_standardness_parameters const& p = {}) {
        return policy::check_transaction_standardness_bchn(make_tx({in()}, std::move(outputs)), descartes, p);
    };

    SECTION("single output of 223 bytes (220 bytes of payload) is standard, 224 is not") {
        CHECK(null_data_script(223).serialized_size(false) == 223);
        CHECK(check({out(0, null_data_script(223))}) == error::success);
        CHECK(is_error(check({out(0, null_data_script(224))}), error::standardness_datacarrier_size));
    }

    SECTION("the limit is the aggregate of all the OP_RETURN scripts of the transaction") {
        CHECK(check({out(0, null_data_script(112)), out(0, null_data_script(111))}) == error::success);
        CHECK(is_error(check({out(0, null_data_script(112)), out(0, null_data_script(112))}), error::standardness_datacarrier_size));
        CHECK(check({out(0, null_data_script(100)), out(0, null_data_script(100)), out(0, null_data_script(23))}) == error::success);
        CHECK(is_error(check({out(0, null_data_script(100)), out(0, null_data_script(100)), out(0, null_data_script(24))}),
                       error::standardness_datacarrier_size));
    }

    SECTION("there is no limit on the number of OP_RETURN outputs, only on their size") {
        std::vector<chain::output> many;
        for (int i = 0; i < 223; ++i) {
            many.push_back(out(0, null_data_script(1)));
        }
        CHECK(check(many) == error::success);
        many.push_back(out(0, null_data_script(1)));
        CHECK(is_error(check(many), error::standardness_datacarrier_size));
    }

    SECTION("other outputs do not count") {
        CHECK(check({out(0, null_data_script(223)), out(100'000, p2pkh_script())}) == error::success);
    }

    SECTION("configured limit") {
        policy::bchn_standardness_parameters p;
        p.maximum_datacarrier_bytes = 100;
        CHECK(check({out(0, null_data_script(100))}, p) == error::success);
        CHECK(is_error(check({out(0, null_data_script(101))}, p), error::standardness_datacarrier_size));
        // transaction_tests.cpp test_IsStandard also sets nMaxDatacarrierBytes to 90.
        p.maximum_datacarrier_bytes = 90;
        CHECK(check({out(0, null_data_script(90))}, p) == error::success);
        CHECK(is_error(check({out(0, null_data_script(91))}, p), error::standardness_datacarrier_size));
        CHECK(check({out(0, null_data_script(45)), out(0, null_data_script(45))}, p) == error::success);
        CHECK(is_error(check({out(0, null_data_script(45)), out(0, null_data_script(46))}, p), error::standardness_datacarrier_size));
        p.maximum_datacarrier_bytes = 0;
        CHECK(is_error(check({out(0, null_data_script(1))}, p), error::standardness_datacarrier_size));
        CHECK(check({out(100'000, p2pkh_script())}, p) == error::success);
        p = {};
        CHECK(check({out(0, null_data_script(223))}, p) == error::success);
    }

    SECTION("an earlier dust output is reported before the aggregate size, which is checked at the end") {
        CHECK(is_error(check({out(0, null_data_script(224)), out(1, p2pkh_script())}), error::standardness_dust));
        CHECK(is_error(check({out(0, null_data_script(224)), out(1, generic_script(10))}), error::standardness_scriptpubkey));
    }

    SECTION("an OP_RETURN with a non push tail is nonstandard before upgrade 12, a plain script after it") {
        raw not_push_only;
        not_push_only.op(OP_RETURN).push({75}).op(OP_ADD);
        CHECK(is_error(check({out(0, not_push_only.script())}), error::standardness_scriptpubkey));
        auto const tx = make_tx({in()}, {out(0, not_push_only.script())});
        CHECK(policy::check_transaction_standardness_bchn(tx, upgrade_12) == error::success);
    }
}

TEST_CASE("BCHN standardness - order of the checks", "[bchn_non_vm][standardness_bchn]") {
    // version before size
    auto huge = tx_of_size(100'001);
    auto const bad_version = chain::transaction(3, 0, huge.inputs(), huge.outputs());
    CHECK(is_error(policy::check_transaction_standardness_bchn(bad_version, descartes), error::standardness_version));
    // size before scriptSig (every input here is push only, the first is oversized)
    auto const oversized_tx = chain::transaction(1, 0, huge.inputs(), huge.outputs());
    CHECK(is_error(policy::check_transaction_standardness_bchn(oversized_tx, descartes), error::standardness_transaction_size));
    // scriptSig before outputs
    auto const bad_both = make_tx({in(raw{}.op(OP_NOP).script())}, {out(1, generic_script(10))});
    CHECK(is_error(policy::check_transaction_standardness_bchn(bad_both, descartes), error::standardness_scriptsig_not_push_only));
    // output template before dust, in the same output
    CHECK(is_error(policy::check_transaction_standardness_bchn(make_tx({in()}, {out(0, generic_script(10))}), descartes),
                   error::standardness_scriptpubkey));
    // no inputs and no outputs: nothing to reject
    CHECK(policy::check_transaction_standardness_bchn(make_tx({}, {}), descartes) == error::success);
}

// AreInputsStandard
//-----------------------------------------------------------------------------

TEST_CASE("BCHN standardness - inputs", "[bchn_non_vm][standardness_bchn]") {
    SECTION("coinbase is standard") {
        auto const coinbase = make_tx({chain::input(chain::output_point(null_hash, chain::point::null_index), raw{}.push(filler(10)).script(), 0xffffffff)}, {out(1000, p2pkh_script())});
        REQUIRE(coinbase.is_coinbase());
        CHECK(policy::check_input_standardness_bchn(coinbase, descartes) == error::success);
        CHECK(policy::check_input_standardness_bchn(coinbase, before_tokens) == error::success);
    }

    SECTION("templates of the previous outputs") {
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, p2pkh_script())}), descartes) == error::success);
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, p2pkh_script()), out(5, p2sh_script()), out(0, p2pk_script())}), descartes) == error::success);
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, null_data_script(10))}), descartes) == error::success);
        CHECK(is_error(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, generic_script(10))}), descartes), error::standardness_inputs));
        // pay to script is a template only after upgrade 12
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, generic_script(10))}), upgrade_12) == error::success);
        CHECK(is_error(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, generic_script(202))}), upgrade_12), error::standardness_inputs));
        // p2sh_32 is a template only with its flag
        CHECK(is_error(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, p2sh32_script())}), before_tokens), error::standardness_inputs));
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(1000, p2sh32_script())}), descartes) == error::success);
    }

    SECTION("only the templates and the tokens are checked, no m or n limits and no dust") {
        data_chunk const k[4] = {compressed_key(1), compressed_key(2), compressed_key(3), compressed_key(4)};
        auto const one_of_four = raw{}.op(OP_1).push(k[0]).push(k[1]).push(k[2]).push(k[3]).op(OP_4).op(OP_CHECKMULTISIG).script();
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(0, one_of_four)}), descartes) == error::success);
        CHECK(policy::check_input_standardness_bchn(spend_with_prevouts({out(0, p2pkh_script())}), descartes) == error::success);
    }

    SECTION("tokens in a previous output before activation") {
        auto const tx = spend_with_prevouts({token_out(1000, p2pkh_script())});
        CHECK(is_error(policy::check_input_standardness_bchn(tx, before_tokens), error::standardness_inputs));
        CHECK(policy::check_input_standardness_bchn(tx, descartes) == error::success);
    }

    SECTION("the first failing input decides") {
        CHECK(is_error(policy::check_input_standardness_bchn(spend_with_prevouts({out(1, generic_script(10)), out(1, p2pkh_script())}), descartes), error::standardness_inputs));
        // a nonstandard input before a missing one
        auto const tx = spend_with_prevouts({out(1, generic_script(10)), chain::output{}});
        CHECK(is_error(policy::check_input_standardness_bchn(tx, descartes), error::standardness_inputs));
    }

    SECTION("a previous output which is not in the cache is reported, not asserted") {
        auto const tx = spend_with_prevouts({out(1, p2pkh_script()), chain::output{}});
        CHECK(is_error(policy::check_input_standardness_bchn(tx, descartes), error::missing_previous_output));
        CHECK(is_error(policy::check_input_standardness_bchn(make_tx({in()}, {out(1, p2pkh_script())}), descartes), error::missing_previous_output));
    }
}

// Sizes and flags
//-----------------------------------------------------------------------------

TEST_CASE("BCHN standardness - virtual transaction size", "[bchn_non_vm][standardness_bchn]") {
    // max(size, sigchecks * bytes_per_sigcheck)
    CHECK(policy::get_virtual_transaction_size_bchn(int64_t(1000), 0) == 1000);
    CHECK(policy::get_virtual_transaction_size_bchn(int64_t(1000), 20) == 1000);
    CHECK(policy::get_virtual_transaction_size_bchn(int64_t(1000), 21) == 1050);
    CHECK(policy::get_virtual_transaction_size_bchn(int64_t(1000), 21, 25) == 1000);
    CHECK(policy::get_virtual_transaction_size_bchn(int64_t(1000), 41, 25) == 1025);
    CHECK(policy::get_virtual_transaction_size_bchn(int64_t(0), 0) == 0);

    auto const tx = simple_tx();
    auto const size = int64_t(tx.serialized_size(true));
    CHECK(policy::get_virtual_transaction_size_bchn(tx, 0) == size);
    CHECK(policy::get_virtual_transaction_size_bchn(tx, 1) == size);
    CHECK(policy::get_virtual_transaction_size_bchn(tx, size / 50 + 1) == (size / 50 + 1) * 50);

    auto const input = in();
    auto const input_size = int64_t(input.serialized_size(true));
    REQUIRE(input_size == 36 + 1 + 11 + 4);
    CHECK(policy::get_virtual_transaction_input_size_bchn(input, 0) == input_size);
    CHECK(policy::get_virtual_transaction_input_size_bchn(input, 1) == input_size);
    CHECK(policy::get_virtual_transaction_input_size_bchn(input, 2) == 100);
}

TEST_CASE("BCHN standardness - script flags", "[bchn_non_vm][standardness_bchn]") {
    auto const mandatory = policy::bchn_mandatory_script_flags();
    auto const standard = policy::bchn_standard_script_flags();

    // BCHN MANDATORY_SCRIPT_VERIFY_FLAGS
    for (auto flag : {script_flags::bip16_rule, script_flags::bch_strictenc, script_flags::bch_sighash_forkid, script_flags::bch_low_s,
                      script_flags::bch_nullfail, script_flags::bch_minimaldata, script_flags::bch_schnorr_multisig,
                      script_flags::bch_enforce_sigchecks}) {
        CHECK((mandatory & flag) == flag);
    }
    CHECK(mandatory == (script_flags::bip16_rule | script_flags::bch_strictenc | script_flags::bch_sighash_forkid | script_flags::bch_low_s |
                        script_flags::bch_nullfail | script_flags::bch_minimaldata | script_flags::bch_schnorr_multisig |
                        script_flags::bch_enforce_sigchecks));

    // STANDARD_SCRIPT_VERIFY_FLAGS: mandatory plus DERSIG, SIGPUSHONLY, DISCOURAGE_UPGRADABLE_NOPS, CLEANSTACK, CLTV, CSV,
    // DISALLOW_SEGWIT_RECOVERY and INPUT_SIGCHECKS. Not MINIMALIF.
    CHECK((standard & mandatory) == mandatory);
    auto const standard_only = standard & ~mandatory;
    CHECK(standard_only == (script_flags::bip66_rule | script_flags::bch_sigpushonly | script_flags::bch_discourage_upgradable_nops |
                            script_flags::bch_cleanstack | script_flags::bip65_rule | script_flags::bip112_rule |
                            script_flags::bch_disallow_segwit_recovery | script_flags::bch_input_sigchecks));
    CHECK((standard & script_flags::bch_minimalif) == 0);
    CHECK((standard & script_flags::bch_vm_limits_standard) == 0);

    // GetMemPoolScriptFlags: consensus flags | standard flags, plus the strict VM limits when they are active
    CHECK(policy::get_standard_script_flags_bchn(descartes) == (descartes | standard));
    CHECK(policy::get_standard_script_flags_bchn(0) == standard);
    auto const galois = descartes | script_flags::bch_vm_limits | script_flags::bch_bigint;
    CHECK(policy::get_standard_script_flags_bchn(galois) == (galois | standard | script_flags::bch_vm_limits_standard));
    CHECK(policy::get_standard_script_flags_bchn(galois, false) == (galois | standard));
    // consensus flags are preserved, the policy ones are only added
    CHECK((policy::get_standard_script_flags_bchn(upgrade_12) & upgrade_12) == upgrade_12);
}

// Constants
//-----------------------------------------------------------------------------

TEST_CASE("BCHN standardness - constants", "[bchn_non_vm][standardness_bchn]") {
    static_assert(policy::bchn_max_standard_tx_size == 100'000);
    static_assert(policy::bchn_max_tx_in_script_sig_size_legacy == 1650);
    static_assert(policy::bchn_max_op_return_relay == 223);
    static_assert(policy::bchn_dust_relay_tx_fee == 1000);
    static_assert(policy::bchn_default_bytes_per_sigcheck == 50);
    static_assert(policy::bchn_dust_spend_input_size == 148);
    static_assert(policy::bchn_max_script_size == 10'000);
    static_assert(policy::bchn_max_p2s_script_size == 201);

    policy::bchn_standardness_parameters defaults;
    CHECK(defaults.maximum_datacarrier_bytes == 223);
    CHECK(defaults.dust_relay_fee_per_kb == 1000);
    CHECK(defaults.permit_bare_multisig);
    CHECK(defaults.bytes_per_sigcheck == 50);
}

#endif // KTH_CURRENCY_BCH
