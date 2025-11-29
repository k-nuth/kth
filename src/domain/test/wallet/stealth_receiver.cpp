// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>

#include <test_helpers.hpp>

#include <kth/infrastructure/wallet/hd_private.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: stealth receiver tests

constexpr char main_key[] = "tprv8ctN3HAF9dCgX9ggdCwiZHa7c3UHuG2Ev4jgYWDhTHDUVWKKsg7znbr3vYtmCzVqcMQsjd9cSKsyKGaDvTAUMkw1UphETe1j8LcT21eWPkH";
constexpr char stealth_address_str[] = "vJmudwspxzmEoz1AP5tTrRMcuop6XjNWa1SnjHFmLeSc9DAkro6J6oYnD7MubLHx9wT3rm7D6xgA8U9Lr9zjzijhVSuUbYdMNYUN27";
constexpr char ephemeral_private[] = "f91e673103863bbeb0ef1852cd8eade6b73ea55afc9b1873be62bf628eac072a";
constexpr char receiver_private[] = "fc696c9f7143916f24977210c806101866c7fa13cc06982978d80518c91af2fb";
constexpr char derived_address[] = "mtKffkQLTw2D6f6mTkrWfi8qxLv4jL1LrK";

// // TODO(legacy): test individual methods in isolation.
// TEST_CASE("stealth receiver exchange between sender and receiver always round trips", "[stealth receiver]") {
//     static auto const version = payment_address::testnet_p2kh;
//     const hd_private hd_main_key(main_key, hd_private::testnet);
//     auto const scan_key = hd_main_key.derive_private(0 + hd_first_hardened_key);
//     auto const spend_key = hd_main_key.derive_private(1 + hd_first_hardened_key);
//     auto const& scan_private = scan_key.secret();
//     auto const& spend_private = spend_key.secret();

//     const stealth_receiver receiver(scan_private, spend_private, binary{}, version);
//     REQUIRE(receiver);

//     auto const& address = receiver.stealth_address();
//     REQUIRE(address.encoded() == stealth_address_str);

//     // Instead of generating a random ephemeral_private, use this one.
//     ec_secret ephemeral_secret;
//     REQUIRE(decode_base16(ephemeral_secret, ephemeral_private));

//     // Sender sends BTC to send_address and the preceding output is
//     // ephemeral_public right-padded up to 80 bytes total (max standard op_return).
//     const stealth_sender sender(ephemeral_secret, address, data_chunk{}, binary{}, version);
//     REQUIRE(sender);

//     auto const& payment = sender.payment_address();
//     REQUIRE(payment.encoded_legacy() == derived_address);

//     // Receiver scans blockchain to get a list of potentially-matching values.
//     // client.fetch_stealth() will yield rows of:
//     // [ephemkey:32] [address:20] [tx_hash:32]
//     // Normally this is obtained by the server via client.fetch_stealth.
//     ec_compressed ephemeral_public;
//     REQUIRE(extract_ephemeral_key(ephemeral_public, sender.stealth_script()));

//     // The receiver can regenerate send_address using just ephemeral_public.
//     payment_address derived_addr;
//     REQUIRE(receiver.derive_address(derived_addr, ephemeral_public));
//     REQUIRE(derived_addr == sender.payment_address());

//     // Only reciever can derive stealth private, as it requires both scan and
//     // spend private keys.
//     ec_secret recv_private;
//     REQUIRE(receiver.derive_private(recv_private, ephemeral_public));

//     ec_compressed receiver_public;
//     REQUIRE(secret_to_public(receiver_public, recv_private));

//     // The receiver now has the stealth private key and the send address.
//     REQUIRE(encode_base16(recv_private) == receiver_private);
//     REQUIRE(payment_address(ec_public{receiver_public}, version) == derived_addr);
// }

// End Test Suite
