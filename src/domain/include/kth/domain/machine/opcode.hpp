// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_OPCODE_HPP
#define KTH_DOMAIN_MACHINE_OPCODE_HPP

#include <cstdint>
#include <string>

#include <kth/domain/define.hpp>
#include <kth/domain/machine/rule_fork.hpp>

#include <kth/domain/define.hpp>
//#include <kth/infrastructure/define.hpp>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::machine {

/// Determine if the fork is enabled in the active forks set.
//TODO(fernando): duplicated in chain::script (domain)
static
bool is_enabled(uint32_t active_forks, rule_fork fork) {
    return (fork & active_forks) != 0;
}

enum class opcode : uint8_t {
    //-------------------------------------------------------------------------
    // is_relaxed_push, is_push (excluding reserved_80)

// push value
    push_size_0 = 0x00,     // 0        // is_version (pushes [] to the stack, not 0)
    push_size_1 = 0x01,     // 1
    push_size_2 = 0x02,     // 2
    push_size_3 = 0x03,     // 3
    push_size_4 = 0x04,     // 4
    push_size_5 = 0x05,     // 5
    push_size_6 = 0x06,     // 6
    push_size_7 = 0x07,     // 7
    push_size_8 = 0x08,     // 8
    push_size_9 = 0x09,     // 9
    push_size_10 = 0x0a,    // 10
    push_size_11 = 0x0b,    // 11
    push_size_12 = 0x0c,    // 12
    push_size_13 = 0x0d,    // 13
    push_size_14 = 0x0e,    // 14
    push_size_15 = 0x0f,    // 15
    push_size_16 = 0x10,    // 16
    push_size_17 = 0x11,    // 17
    push_size_18 = 0x12,    // 18
    push_size_19 = 0x13,    // 19
    push_size_20 = 0x14,    // 20
    push_size_21 = 0x15,    // 21
    push_size_22 = 0x16,    // 22
    push_size_23 = 0x17,    // 23
    push_size_24 = 0x18,    // 24
    push_size_25 = 0x19,    // 25
    push_size_26 = 0x1a,    // 26
    push_size_27 = 0x1b,    // 27
    push_size_28 = 0x1c,    // 28
    push_size_29 = 0x1d,    // 29
    push_size_30 = 0x1e,    // 30
    push_size_31 = 0x1f,    // 31
    push_size_32 = 0x20,    // 32
    push_size_33 = 0x21,    // 33
    push_size_34 = 0x22,    // 34
    push_size_35 = 0x23,    // 35
    push_size_36 = 0x24,    // 36
    push_size_37 = 0x25,    // 37
    push_size_38 = 0x26,    // 38
    push_size_39 = 0x27,    // 39
    push_size_40 = 0x28,    // 40
    push_size_41 = 0x29,    // 41
    push_size_42 = 0x2a,    // 42
    push_size_43 = 0x2b,    // 43
    push_size_44 = 0x2c,    // 44
    push_size_45 = 0x2d,    // 45
    push_size_46 = 0x2e,    // 46
    push_size_47 = 0x2f,    // 47
    push_size_48 = 0x30,    // 48
    push_size_49 = 0x31,    // 49
    push_size_50 = 0x32,    // 50
    push_size_51 = 0x33,    // 51
    push_size_52 = 0x34,    // 52
    push_size_53 = 0x35,    // 53
    push_size_54 = 0x36,    // 54
    push_size_55 = 0x37,    // 55
    push_size_56 = 0x38,    // 56
    push_size_57 = 0x39,    // 57
    push_size_58 = 0x3a,    // 58
    push_size_59 = 0x3b,    // 59
    push_size_60 = 0x3c,    // 60
    push_size_61 = 0x3d,    // 61
    push_size_62 = 0x3e,    // 62
    push_size_63 = 0x3f,    // 63
    push_size_64 = 0x40,    // 64
    push_size_65 = 0x41,    // 65
    push_size_66 = 0x42,    // 66
    push_size_67 = 0x43,    // 67
    push_size_68 = 0x44,    // 68
    push_size_69 = 0x45,    // 69
    push_size_70 = 0x46,    // 70
    push_size_71 = 0x47,    // 71
    push_size_72 = 0x48,    // 72
    push_size_73 = 0x49,    // 73
    push_size_74 = 0x4a,    // 74
    push_size_75 = 0x4b,    // 75
    push_one_size = 0x4c,   // 76
    push_two_size = 0x4d,   // 77
    push_four_size = 0x4e,  // 78

    push_negative_1 = 0x4f, // 79 is_numeric
    reserved_80 = 0x50,     // 80 [reserved]

    push_positive_1 = 0x51, // 81, is_numeric, is_positive, is_version
    push_positive_2 = 0x52, // 82, is_numeric, is_positive, is_version
    push_positive_3 = 0x53, // 83, is_numeric, is_positive, is_version
    push_positive_4 = 0x54, // 84, is_numeric, is_positive, is_version
    push_positive_5 = 0x55, // 85, is_numeric, is_positive, is_version
    push_positive_6 = 0x56, // 86, is_numeric, is_positive, is_version
    push_positive_7 = 0x57, // 87, is_numeric, is_positive, is_version
    push_positive_8 = 0x58, // 88, is_numeric, is_positive, is_version
    push_positive_9 = 0x59, // 89, is_numeric, is_positive, is_version
    push_positive_10 = 0x5a, // 90, is_numeric, is_positive, is_version
    push_positive_11 = 0x5b, // 91, is_numeric, is_positive, is_version
    push_positive_12 = 0x5c, // 92, is_numeric, is_positive, is_version
    push_positive_13 = 0x5d, // 93, is_numeric, is_positive, is_version
    push_positive_14 = 0x5e, // 94, is_numeric, is_positive, is_version
    push_positive_15 = 0x5f, // 95, is_numeric, is_positive, is_version
    push_positive_16 = 0x60, // 96, is_numeric, is_positive, is_version

    //-------------------------------------------------------------------------
    // is_counted

// control
    nop = 0x61,               // 97
    reserved_98 = 0x62,       // 98 [ver]
    if_ = 0x63,               // 99 is_conditional
    notif = 0x64,             // 100 is_conditional
    disabled_verif = 0x65,    // 101 is_disabled
    disabled_vernotif = 0x66, // 102 is_disabled
    else_ = 0x67,             // 103 is_conditional
    endif = 0x68,             // 104 is_conditional
    verify = 0x69,            // 105
    return_ = 0x6a,           // 106

// stack ops
    toaltstack = 0x6b,        // 107
    fromaltstack = 0x6c,      // 108
    drop2 = 0x6d,             // 109
    dup2 = 0x6e,              // 110
    dup3 = 0x6f,              // 111
    over2 = 0x70,             // 112
    rot2 = 0x71,              // 113
    swap2 = 0x72,             // 114
    ifdup = 0x73,             // 115
    depth = 0x74,             // 116
    drop = 0x75,              // 117
    dup = 0x76,               // 118
    nip = 0x77,               // 119
    over = 0x78,              // 120
    pick = 0x79,              // 121
    roll = 0x7a,              // 122
    rot = 0x7b,               // 123
    swap = 0x7c,              // 124
    tuck = 0x7d,              // 125

// splice ops
    cat = 0x7e,             // 126
    split = 0x7f,           // 127 was called substr before (disabled and re-enabled after pythagoras/monolith upgrade, May 2018)
    num2bin = 0x80,         // 128 was called left before (disabled and re-enabled after pythagoras/monolith upgrade, May 2018)
    bin2num = 0x81,         // 129 was called right before (disabled and re-enabled after pythagoras/monolith upgrade, May 2018)
    size = 0x82,            // 130

// bit logic
    disabled_invert = 0x83, // 131, is_disabled
    and_ = 0x84,            // 132, disabled and re-enabled after pythagoras/monolith upgrade, May 2018
    or_ = 0x85,             // 133, disabled and re-enabled after pythagoras/monolith upgrade, May 2018
    xor_ = 0x86,            // 134, disabled and re-enabled after pythagoras/monolith upgrade, May 2018
    equal = 0x87,           // 135
    equalverify = 0x88,     // 136
    reserved_137 = 0x89,    // 137 [reserved1]
    reserved_138 = 0x8a,    // 138 [reserved2]

// numeric
    add1 = 0x8b,            //  139
    sub1 = 0x8c,            // 140
    disabled_mul2 = 0x8d,   // 141 is_disabled
    disabled_div2 = 0x8e,   // 142 is_disabled
    negate = 0x8f,          // 143
    abs = 0x90,             // 144
    not_ = 0x91,            // 145
    nonzero = 0x92,         // 146

    add = 0x93,             // 147
    sub = 0x94,             // 148
    mul = 0x95,             // 149, disabled and re-enabled after gauss/upgrade8 upgrade, May 2022
    div = 0x96,             // 150, disabled and re-enabled after pythagoras/monolith upgrade, May 2018
    mod = 0x97,             // 151, disabled and re-enabled after pythagoras/monolith upgrade, May 2018
    disabled_lshift = 0x98, // 152 is_disabled
    disabled_rshift = 0x99, // 153 is_disabled

    booland = 0x9a,            // 154
    boolor = 0x9b,             // 155
    numequal = 0x9c,           // 156
    numequalverify = 0x9d,     // 157
    numnotequal = 0x9e,        // 158
    lessthan = 0x9f,           // 159
    greaterthan = 0xa0,        // 160
    lessthanorequal = 0xa1,    // 161
    greaterthanorequal = 0xa2, // 162
    min = 0xa3,                // 163
    max = 0xa4,                // 164

    within = 0xa5,             // 165

// crypto
    ripemd160 = 0xa6,           // 166
    sha1 = 0xa7,                // 167
    sha256 = 0xa8,              // 168
    hash160 = 0xa9,             // 169
    hash256 = 0xaa,             // 170
    codeseparator = 0xab,       // 171
    checksig = 0xac,            // 172
    checksigverify = 0xad,      // 173
    checkmultisig = 0xae,       // 174
    checkmultisigverify = 0xaf, // 175

// expansion
    nop1 = 0xb0,                 // 176
    nop2 = 0xb1,                 // 177
    checklocktimeverify = nop2,  // 177
    nop3 = 0xb2,                 // 178
    checksequenceverify = nop3,  // 178
    nop4 = 0xb3,                 // 179
    nop5 = 0xb4,                 // 180
    nop6 = 0xb5,                 // 181
    nop7 = 0xb6,                 // 182
    nop8 = 0xb7,                 // 183
    nop9 = 0xb8,                 // 184
    nop10 = 0xb9,                // 185

// more crypto
    checkdatasig = 0xba,         // 186
    checkdatasigverify = 0xbb,   // 187

// additional byte string operations
    reverse_bytes = 0xbc,         // 188

// Available codepoints
    // 0xbd,                     // 189
    // 0xbe,                     // 190
    // 0xbf,                     // 191

// Native Introspection opcodes
    input_index = 0xc0,           // 192
    active_bytecode = 0xc1,       // 193
    tx_version = 0xc2,            // 194
    tx_input_count = 0xc3,        // 195
    tx_output_count = 0xc4,       // 196
    tx_locktime = 0xc5,           // 197
    utxo_value = 0xc6,            // 198
    utxo_bytecode = 0xc7,         // 199
    outpoint_tx_hash = 0xc8,      // 200
    outpoint_index = 0xc9,        // 201
    input_bytecode = 0xca,        // 202
    input_sequence_number = 0xcb, // 203
    output_value = 0xcc,          // 204
    output_bytecode = 0xcd,       // 205

// Native Introspection of tokens (SCRIPT_ENABLE_TOKENS must be set)
    utxo_token_category = 0xce,     // 206
    utxo_token_commitment = 0xcf,   // 207
    utxo_token_amount = 0xd0,       // 208
    output_token_category = 0xd1,   // 209
    output_token_commitment = 0xd2, // 210
    output_token_amount = 0xd3,     // 211

    reserved_212 = 0xd4,          // 212 [reserved3]
    reserved_213 = 0xd5,          // 213 [reserved4]
    reserved_214 = 0xd6,          // 214

// The first op_code value after all defined opcodes
    first_undefined_op_value = reserved_214, // 0xd6 214

    reserved_215 = 0xd7,          // 215
    reserved_216 = 0xd8,          // 216
    reserved_217 = 0xd9,          // 217
    reserved_218 = 0xda,          // 218
    reserved_219 = 0xdb,          // 219
    reserved_220 = 0xdc,          // 220
    reserved_221 = 0xdd,          // 221
    reserved_222 = 0xde,          // 222
    reserved_223 = 0xdf,          // 223
    reserved_224 = 0xe0,          // 224
    reserved_225 = 0xe1,          // 225
    reserved_226 = 0xe2,          // 226
    reserved_227 = 0xe3,          // 227
    reserved_228 = 0xe4,          // 228
    reserved_229 = 0xe5,          // 229
    reserved_230 = 0xe6,          // 230
    reserved_231 = 0xe7,          // 231
    reserved_232 = 0xe8,          // 232
    reserved_233 = 0xe9,          // 233
    reserved_234 = 0xea,          // 234
    reserved_235 = 0xeb,          // 235
    reserved_236 = 0xec,          // 236
    reserved_237 = 0xed,          // 237
    reserved_238 = 0xee,          // 238
    reserved_239 = 0xef,          // 239

// Invalid opcode if executed, but used for special token prefix if at
// position 0 in scriptPubKey. See: primitives/token.h
    special_token_prefix = reserved_239, // 0xef 239

    reserved_240 = 0xf0,        // 240
    reserved_241 = 0xf1,        // 241
    reserved_242 = 0xf2,        // 242
    reserved_243 = 0xf3,        // 243
    reserved_244 = 0xf4,        // 244
    reserved_245 = 0xf5,        // 245
    reserved_246 = 0xf6,        // 246
    reserved_247 = 0xf7,        // 247
    reserved_248 = 0xf8,        // 248
    reserved_249 = 0xf9,        // 249
    reserved_250 = 0xfa,        // 250
    reserved_251 = 0xfb,        // 251
    reserved_252 = 0xfc,        // 252
    reserved_253 = 0xfd,        // 253
    reserved_254 = 0xfe,        // 254
    reserved_255 = 0xff,        // 255

    invalidopcode = reserved_255,  //  0xff 255  < Not a real OPCODE!
};

/// Convert the opcode to a mnemonic string.
KD_API std::string opcode_to_string(opcode value, uint32_t active_forks);

/// Convert a string to an opcode.
KD_API bool opcode_from_string(opcode& out_code, std::string const& value);

/// Convert any opcode to a string hexadecimal representation.
KD_API std::string opcode_to_hexadecimal(opcode code);

/// Convert any hexadecimal byte to an opcode.
KD_API bool opcode_from_hexadecimal(opcode& out_code, std::string const& value);

} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_OPCODE_HPP
