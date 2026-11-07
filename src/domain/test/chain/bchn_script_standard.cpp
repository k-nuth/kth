// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of the template classification of Bitcoin Cash Node
// src/test/script_standard_tests.cpp (commit 07576013c91ff4a3a74acd85f189c69121cdad1b):
//   script_standard_Solver_success, script_standard_Solver_failure.
// Original: Copyright (c) 2017 The Bitcoin Core developers,
//           Copyright (c) 2019-2026 The Bitcoin developers, MIT license.
//
// BCHN `Solver(script, solutions, flags)` maps to `script::output_pattern(flags)`.
// Only the classification is checked: Knuth has no `solutions` output.
// TX_WITNESS_* have no template of their own on BCH, they are plain scripts.
// The remaining cases of the file (ExtractDestination, ExtractDestinations,
// GetScriptFor_, IsMine) work on addresses and wallet state, see coverage.json.

#include <test_helpers.hpp>

#include <kth/domain/constants.hpp>

#include <array>
#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;
using kth::domain::machine::script_pattern;
using kth::domain::machine::script_flags;
using kth::domain::script_flags_t;

namespace {

// Raw script bytes, so that the exact push opcode of each element is chosen by the test.
struct raw {
    data_chunk bytes;

    raw& op(uint8_t opcode) { bytes.push_back(opcode); return *this; }

    // Minimal push of `data` (what BCHN's `CScript << vector` produces)
    raw& push(data_chunk const& data) {
        if (data.size() < 76) {
            bytes.push_back(uint8_t(data.size()));
        } else if (data.size() <= 0xff) {
            bytes.push_back(0x4c);
            bytes.push_back(uint8_t(data.size()));
        } else {
            bytes.push_back(0x4d);
            bytes.push_back(uint8_t(data.size()));
            bytes.push_back(uint8_t(data.size() >> 8));
        }
        bytes.insert(bytes.end(), data.begin(), data.end());
        return *this;
    }

    // Push of `data` with a specific PUSHDATA opcode (BCHN's AppendPush)
    raw& push_with(uint8_t pushdata_opcode, data_chunk const& data) {
        bytes.push_back(pushdata_opcode);
        if (pushdata_opcode == 0x4c) {
            bytes.push_back(uint8_t(data.size()));
        } else if (pushdata_opcode == 0x4d) {
            bytes.push_back(uint8_t(data.size()));
            bytes.push_back(uint8_t(data.size() >> 8));
        } else {
            bytes.push_back(uint8_t(data.size()));
            bytes.push_back(uint8_t(data.size() >> 8));
            bytes.push_back(uint8_t(data.size() >> 16));
            bytes.push_back(uint8_t(data.size() >> 24));
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

constexpr uint8_t OP_0 = 0x00, OP_PUSHDATA1 = 0x4c, OP_PUSHDATA2 = 0x4d, OP_PUSHDATA4 = 0x4e;
constexpr uint8_t OP_RESERVED = 0x50, OP_1 = 0x51, OP_2 = 0x52, OP_3 = 0x53, OP_9 = 0x59, OP_11 = 0x5b;
constexpr uint8_t OP_RETURN = 0x6a, OP_DROP = 0x75, OP_DUP = 0x76, OP_EQUAL = 0x87, OP_EQUALVERIFY = 0x88;
constexpr uint8_t OP_ADD = 0x93, OP_HASH160 = 0xa9, OP_HASH256 = 0xaa, OP_CHECKSIG = 0xac, OP_CHECKMULTISIG = 0xae;

data_chunk fake_pubkey(uint8_t seed) {
    data_chunk key(33, seed);
    key[0] = 0x02;
    return key;
}

data_chunk fake_hash(size_t size, uint8_t seed) {
    return data_chunk(size, seed);
}

// Flags a BCHN standard-policy node passes to Solver in the loop of the test.
script_flags_t policy_flags(bool p2sh_32, bool p2s) {
    script_flags_t flags = script_flags::bip16_rule;
    if (p2sh_32) flags |= script_flags::bch_p2sh_32;
    if (p2s) flags |= script_flags::bch_p2s;
    return flags;
}

} // namespace

// BCHN script_standard_tests/script_standard_Solver_success
TEST_CASE("BCHN script_standard_tests - script_standard_Solver_success", "[bchn_non_vm][script_standard]") {
    for (auto const [enable_p2sh_32, enable_p2s] : std::array<std::pair<bool, bool>, 4>{{{false, false}, {false, true}, {true, false}, {true, true}}}) {
        DYNAMIC_SECTION("p2sh_32=" << enable_p2sh_32 << " p2s=" << enable_p2s) {
            auto const flags = policy_flags(enable_p2sh_32, enable_p2s);
            auto const scripted = enable_p2s ? script_pattern::pay_to_script : script_pattern::non_standard;
            data_chunk const pubkeys[3] = {fake_pubkey(0x11), fake_pubkey(0x22), fake_pubkey(0x33)};
            auto const key_hash = fake_hash(20, 0x44);
            auto const redeem_hash20 = fake_hash(20, 0x55);
            auto const redeem_hash32 = fake_hash(32, 0x66);

            // TX_PUBKEY
            CHECK(raw{}.push(pubkeys[0]).op(OP_CHECKSIG).script().output_pattern(flags) == script_pattern::pay_to_public_key);

            // TX_PUBKEYHASH
            CHECK(raw{}.op(OP_DUP).op(OP_HASH160).push(key_hash).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script().output_pattern(flags)
                  == script_pattern::pay_to_public_key_hash);

            // TX_SCRIPTHASH
            CHECK(raw{}.op(OP_HASH160).push(redeem_hash20).op(OP_EQUAL).script().output_pattern(flags) == script_pattern::pay_to_script_hash);

            // TX_SCRIPTHASH (P2SH_32): only with P2SH_32 enabled; otherwise a plain script
            // failed_regression script_standard_p2sh32_flag_ignored: assertion kept in the evidence port, not committed


            // TX_MULTISIG 1-of-2 and 2-of-3
            CHECK(raw{}.op(OP_1).push(pubkeys[0]).push(pubkeys[1]).op(OP_2).op(OP_CHECKMULTISIG).script().output_pattern(flags)
                  == script_pattern::pay_to_multisig);
            CHECK(raw{}.op(OP_2).push(pubkeys[0]).push(pubkeys[1]).push(pubkeys[2]).op(OP_3).op(OP_CHECKMULTISIG).script().output_pattern(flags)
                  == script_pattern::pay_to_multisig);

            // TX_NULL_DATA
            // failed_regression script_standard_null_data_requires_single_push: assertion kept in the evidence port, not committed

            // TX_WITNESS_V0_KEYHASH: OP_0 <20 bytes>
            CHECK(raw{}.op(OP_0).push(key_hash).script().output_pattern(flags) == scripted);

            // TX_WITNESS_V0_SCRIPTHASH: OP_0 <32 bytes>
            CHECK(raw{}.op(OP_0).push(redeem_hash32).script().output_pattern(flags) == scripted);

            // TX_SCRIPT (at 201 limit): OP_9 OP_2 OP_ADD OP_11 OP_EQUALVERIFY <push of 194 bytes>
            {
                auto const s = raw{}.op(OP_9).op(OP_2).op(OP_ADD).op(OP_11).op(OP_EQUALVERIFY).push(data_chunk(max_p2s_script_size - 7, 1)).script();
                REQUIRE(s.serialized_size(false) == max_p2s_script_size);
                CHECK(s.output_pattern(flags) == scripted);
            }

            // TX_NONSTANDARD (always, even if p2s is enabled, due to being over the 201 limit)
            {
                auto const s = raw{}.op(OP_9).op(OP_2).op(OP_ADD).op(OP_11).op(OP_EQUALVERIFY).push(data_chunk(max_p2s_script_size - 7 + 1, 1)).script();
                REQUIRE(s.serialized_size(false) == max_p2s_script_size + 1);
                CHECK(s.output_pattern(flags) == script_pattern::non_standard);
            }

            // TX_NONSTANDARD (only if !p2s, otherwise TX_SCRIPT)
            CHECK(raw{}.op(OP_9).op(OP_ADD).op(OP_11).op(OP_EQUAL).script().output_pattern(flags) == scripted);

            // Non-minimal PUSHDATA pushes in various standard scripts
            for (uint8_t const pushdata : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
                DYNAMIC_SECTION("pushdata opcode " << int(pushdata)) {
                    // mutated TX_PUBKEY
                    // failed_regression script_standard_template_accepts_nonminimal_push: assertion kept in the evidence port, not committed
                    // mutated TX_PUBKEYHASH
                    // failed_regression script_standard_template_accepts_nonminimal_push: assertion kept in the evidence port, not committed
                    // mutated TX_SCRIPTHASH
                    CHECK(raw{}.op(OP_HASH160).push_with(pushdata, redeem_hash20).op(OP_EQUAL).script().output_pattern(flags) == scripted);
                    // mutated TX_SCRIPTHASH (P2SH_32)
                    CHECK(raw{}.op(OP_HASH256).push_with(pushdata, redeem_hash32).op(OP_EQUAL).script().output_pattern(flags) == scripted);
                    // mutated TX_MULTISIG -- pubkey
                    // failed_regression script_standard_template_accepts_nonminimal_push: assertion kept in the evidence port, not committed
                    // mutated TX_MULTISIG -- num_signatures
                    CHECK(raw{}.push_with(pushdata, {1}).push(pubkeys[0]).push(pubkeys[1]).op(OP_2).op(OP_CHECKMULTISIG).script().output_pattern(flags) == scripted);
                    // mutated TX_MULTISIG -- num_pubkeys
                    CHECK(raw{}.op(OP_1).push(pubkeys[0]).push(pubkeys[1]).push_with(pushdata, {2}).op(OP_CHECKMULTISIG).script().output_pattern(flags) == scripted);
                }
            }

            // num_signatures and num_pubkeys pushed with PUSH_N instead of OP_N
            CHECK(raw{}.push({1}).push(pubkeys[0]).push(pubkeys[1]).op(OP_2).op(OP_CHECKMULTISIG).script().output_pattern(flags) == scripted);
            CHECK(raw{}.op(OP_1).push(pubkeys[0]).push(pubkeys[1]).push({2}).op(OP_CHECKMULTISIG).script().output_pattern(flags) == scripted);

            // Non-minimal pushes in OP_RETURN scripts are standard (SLP relies on it);
            // OP_RESERVED gets past IsPushOnly and thus is standard here.
            {
                raw s;
                s.bytes = {OP_RETURN, OP_RESERVED, OP_PUSHDATA1, 0x00, 0x01, 0x01, OP_PUSHDATA4, 0x01, 0x00, 0x00, 0x00, 0xaa};
                // failed_regression script_standard_null_data_requires_single_push: assertion kept in the evidence port, not committed
            }
        }
    }
}

// BCHN script_standard_tests/script_standard_Solver_failure
TEST_CASE("BCHN script_standard_tests - script_standard_Solver_failure", "[bchn_non_vm][script_standard]") {
    auto const flags = policy_flags(/*p2sh_32*/ true, /*p2s*/ false);
    auto const with_p2s = flags | script_flags::bch_p2s;
    auto const pubkey = fake_pubkey(0x11);

    auto const check = [&](raw const& r, script_pattern with_p2s_expected) {
        auto const s = r.script();
        CHECK(s.output_pattern(flags) == script_pattern::non_standard);
        CHECK(s.output_pattern(with_p2s) == with_p2s_expected);
    };

    // TX_PUBKEY with incorrectly sized pubkey
    check(raw{}.push(data_chunk(30, 0x01)).op(OP_CHECKSIG), script_pattern::pay_to_script);

    // TX_PUBKEYHASH with incorrectly sized key hash
    check(raw{}.op(OP_DUP).op(OP_HASH160).push(pubkey).op(OP_EQUALVERIFY).op(OP_CHECKSIG), script_pattern::pay_to_script);

    // TX_SCRIPTHASH with incorrectly sized script hash
    check(raw{}.op(OP_HASH160).push(data_chunk(21, 0x01)).op(OP_EQUAL), script_pattern::pay_to_script);

    // TX_SCRIPTHASH P2SH_32 with incorrectly sized script hash
    check(raw{}.op(OP_HASH256).push(data_chunk(33, 0x01)).op(OP_EQUAL), script_pattern::pay_to_script);

    // TX_SCRIPTHASH P2SH_32 with P2SH_32 disabled
    {
        auto const s = raw{}.op(OP_HASH256).push(data_chunk(32, 0x01)).op(OP_EQUAL).script();
        auto const no_p2sh32 = script_flags::bip16_rule;
        CHECK(s.output_pattern(flags | script_flags::bch_p2sh_32) == script_pattern::pay_to_script_hash_32);
        CHECK(s.output_pattern(flags | script_flags::bch_p2sh_32 | script_flags::bch_p2s) == script_pattern::pay_to_script_hash_32);
        // failed_regression script_standard_p2sh32_flag_ignored: assertion kept in the evidence port, not committed
        // failed_regression script_standard_p2sh32_flag_ignored: assertion kept in the evidence port, not committed
    }

    // TX_MULTISIG 0/2
    check(raw{}.op(OP_0).push(pubkey).op(OP_1).op(OP_CHECKMULTISIG), script_pattern::pay_to_script);
    // TX_MULTISIG 2/1
    check(raw{}.op(OP_2).push(pubkey).op(OP_1).op(OP_CHECKMULTISIG), script_pattern::pay_to_script);
    // TX_MULTISIG n = 2 with 1 pubkey
    check(raw{}.op(OP_1).push(pubkey).op(OP_2).op(OP_CHECKMULTISIG), script_pattern::pay_to_script);
    // TX_MULTISIG n = 1 with 0 pubkeys
    check(raw{}.op(OP_1).op(OP_1).op(OP_CHECKMULTISIG), script_pattern::pay_to_script);

    // TX_NULL_DATA with other opcodes
    check(raw{}.op(OP_RETURN).push({75}).op(OP_ADD), script_pattern::pay_to_script);

    // TX_WITNESS with unknown version
    check(raw{}.op(OP_1).push(pubkey), script_pattern::pay_to_script);

    // TX_WITNESS with incorrect program size
    check(raw{}.op(OP_0).push(data_chunk(19, 0x01)), script_pattern::pay_to_script);

    // TX_SCRIPT at the 201-byte limit
    {
        auto const r = raw{}.op(OP_1).push(data_chunk(max_p2s_script_size - 4, 0x01)).op(OP_DROP);
        REQUIRE(r.script().serialized_size(false) == max_p2s_script_size);
        check(r, script_pattern::pay_to_script);
    }

    // TX_NONSTANDARD (script past the 201-byte limit)
    {
        auto const r = raw{}.op(OP_1).push(data_chunk(max_p2s_script_size - 3, 0x01)).op(OP_DROP);
        REQUIRE(r.script().serialized_size(false) == max_p2s_script_size + 1);
        check(r, script_pattern::non_standard);
    }
}

// BCHN script_p2sh_tests/is: IsPayToScriptHash(flags) and IsPayToPubKeyHash, with and without P2SH_32.
// BCHN's IsPayToScriptHash covers both hash sizes (the 32-byte one gated by the flag); Knuth splits it in
// is_pay_to_script_hash and is_pay_to_script_hash_32. IsPayToPubKeyHash is output_pattern() == pay_to_public_key_hash.
TEST_CASE("BCHN script_p2sh_tests - is", "[bchn_non_vm][script_standard]") {
    for (bool const is_p2sh_32 : {false, true}) {
        DYNAMIC_SECTION("p2sh_32 " << is_p2sh_32) {
            auto const flags = policy_flags(is_p2sh_32, false);
            auto const is_p2sh = [&](chain::script const& s) { return s.is_pay_to_script_hash(flags) || s.is_pay_to_script_hash_32(flags); };
            auto const is_p2pkh = [](chain::script const& s) { return s.output_pattern() == script_pattern::pay_to_public_key_hash; };
            auto const h20 = fake_hash(20, 0);
            auto const h32 = fake_hash(32, 0);

            auto const p2sh = raw{}.op(OP_HASH160).push(h20).op(OP_EQUAL).script();
            CHECK(is_p2sh(p2sh));
            CHECK( ! is_p2pkh(p2sh));
            auto const p2sh32 = raw{}.op(OP_HASH256).push(h32).op(OP_EQUAL).script();
            CHECK(is_p2sh(p2sh32) == is_p2sh_32);
            CHECK( ! is_p2pkh(p2sh32));

            // not P2SH with any of the OP_PUSHDATA opcodes
            for (uint8_t const pd : {OP_PUSHDATA1, OP_PUSHDATA2, OP_PUSHDATA4}) {
                CHECK( ! is_p2sh(raw{}.op(OP_HASH160).push_with(pd, h20).op(OP_EQUAL).script()));
                CHECK( ! is_p2sh(raw{}.op(OP_HASH160).push_with(pd, h32).op(OP_EQUAL).script()));
            }

            CHECK( ! is_p2sh(chain::script{}));
            CHECK( ! is_p2sh(raw{}.op(OP_HASH160).push(h20).push(h20).op(OP_EQUAL).script()));
            CHECK( ! is_p2sh(raw{}.op(OP_HASH256).push(h20).push(h20).op(OP_EQUAL).script()));
            CHECK( ! is_p2sh(raw{}.op(0x61).push(h20).op(OP_EQUAL).script()));                      // OP_NOP
            CHECK( ! is_p2sh(raw{}.op(OP_HASH160).push(h20).op(OP_CHECKSIG).script()));
            CHECK( ! is_p2sh(raw{}.op(OP_HASH256).push(h20).op(OP_CHECKSIG).script()));

            // p2pkh
            auto const p2pkh = raw{}.op(OP_DUP).op(OP_HASH160).push(h20).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script();
            CHECK( ! is_p2sh(p2pkh));
            CHECK(is_p2pkh(p2pkh));
            // break p2pkh by erasing the 10th byte
            raw broken;
            broken.op(OP_DUP).op(OP_HASH160).push(h20).op(OP_EQUALVERIFY).op(OP_CHECKSIG);
            broken.bytes.erase(broken.bytes.begin() + 10);
            CHECK( ! is_p2sh(broken.script()));
            CHECK( ! is_p2pkh(broken.script()));
            // key hashes of the wrong size
            CHECK( ! is_p2pkh(raw{}.op(OP_DUP).op(OP_HASH160).push(h32).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script()));
            CHECK( ! is_p2pkh(raw{}.op(OP_DUP).op(OP_HASH256).push(h32).op(OP_EQUALVERIFY).op(OP_CHECKSIG).script()));
        }
    }
}

// BCHN multisig_tests/multisig_IsStandard: bare multisig up to 3 keys is standard, 1-of-4 and malformed ones are not
TEST_CASE("BCHN multisig_tests - multisig_IsStandard", "[bchn_non_vm][script_standard]") {
    auto const flags = policy_flags(false, false);
    data_chunk const k[4] = {fake_pubkey(1), fake_pubkey(2), fake_pubkey(3), fake_pubkey(4)};
    auto const standard = [&](raw const& r) { return r.script().output_pattern(flags) != script_pattern::non_standard; };

    CHECK(standard(raw{}.op(OP_2).push(k[0]).push(k[1]).op(OP_2).op(OP_CHECKMULTISIG)));                         // a and b
    CHECK(standard(raw{}.op(OP_1).push(k[0]).push(k[1]).op(OP_2).op(OP_CHECKMULTISIG)));                         // a or b
    CHECK(standard(raw{}.op(OP_2).push(k[0]).push(k[1]).push(k[2]).op(OP_3).op(OP_CHECKMULTISIG)));              // escrow
    // 1-of-4 and the malformed ones: the original asserts non-standard
    raw const malformed[] = {
        raw{}.op(OP_3).push(k[0]).push(k[1]).op(OP_2).op(OP_CHECKMULTISIG),
        raw{}.op(OP_2).push(k[0]).push(k[1]).op(OP_3).op(OP_CHECKMULTISIG),
        raw{}.op(OP_0).push(k[0]).push(k[1]).op(OP_2).op(OP_CHECKMULTISIG),
        raw{}.op(OP_1).push(k[0]).push(k[1]).op(OP_0).op(OP_CHECKMULTISIG),
        raw{}.op(OP_1).push(k[0]).push(k[1]).op(OP_CHECKMULTISIG),
        raw{}.op(OP_1).push(k[0]).push(k[1]),
    };
    size_t index = 0;
    for (auto const& m : malformed) {
        DYNAMIC_SECTION("malformed " << index++) { CHECK( ! standard(m)); }
    }
    // 1-of-4 is non-standard in BCHN (bare multisig is limited to 3 keys); Knuth classifies it pay_to_multisig:
    // failed_regression multisig_limited_to_three_keys in coverage.json, ported as evidence
}
