// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// script_tests.cpp script_standard_push (the IsPushOnly half) and script_IsPushOnly_on_invalid_scripts of Bitcoin Cash Node
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b). IsPushOnly decides the P2SH rule "the unlocking script has only pushes" and
// the scriptsig-not-pushonly standardness reason, so it is a script classification, not an interpreter step.
// Original: Copyright (c) 2011-2019 The Bitcoin Core developers, 2017-present The Bitcoin developers, MIT license.
//
// Not ported: the MINIMALDATA verification of each push through VerifyScript (interpreter) of the same case.

#include <test_helpers.hpp>

#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;

namespace {

chain::script script_of(data_chunk const& raw) {
    byte_reader reader(raw);
    auto parsed = chain::script::from_data(reader, false);
    REQUIRE(parsed);
    return std::move(*parsed);
}

// `CScript << ScriptInt(i)`: OP_0, OP_1 .. OP_16, otherwise the minimal little-endian number pushed directly
data_chunk number_push(uint32_t value) {
    if (value == 0) return {0x00};
    if (value <= 16) return {uint8_t(0x50 + value)};
    data_chunk bytes;
    for (uint32_t v = value; v != 0; v >>= 8) bytes.push_back(uint8_t(v));
    if (bytes.back() & 0x80) bytes.push_back(0x00);
    data_chunk out{uint8_t(bytes.size())};
    out.insert(out.end(), bytes.begin(), bytes.end());
    return out;
}

// `CScript << vector`: minimal push of the data
data_chunk data_push(size_t length) {
    data_chunk out;
    if (length == 0) return {0x00};
    if (length < 76) out = {uint8_t(length)};
    else if (length < 256) out = {0x4c, uint8_t(length)};
    else out = {0x4d, uint8_t(length), uint8_t(length >> 8)};
    out.insert(out.end(), length, 0x49);
    return out;
}

} // namespace

TEST_CASE("BCHN script_tests - script_standard_push is push only", "[bchn_non_vm][script]") {
    for (uint32_t i = 0; i < 67000; ++i) {
        auto const script = script_of(number_push(i));
        REQUIRE(chain::script::is_push_only(script.operations()));
    }
    for (size_t length = 0; length <= 520; ++length) {
        auto const script = script_of(data_push(length));
        REQUIRE(chain::script::is_push_only(script.operations()));
    }
}

TEST_CASE("BCHN script_tests - script_IsPushOnly_on_invalid_scripts", "[bchn_non_vm][script]") {
    // a push that announces one byte and has none: not push only
    data_chunk const truncated{0x01};
    byte_reader reader(truncated);
    auto parsed = chain::script::from_data(reader, false);
    REQUIRE(parsed);                                    // Knuth keeps the truncated push as an operation
    CHECK( ! chain::script::is_push_only(parsed->operations()));
    // control: a non-push opcode is not push only
    CHECK( ! chain::script::is_push_only(script_of({0x51, 0x76}).operations()));
}
