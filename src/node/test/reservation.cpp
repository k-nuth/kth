// // Copyright (c) 2016-present Knuth Project developers.
// // Distributed under the MIT software license, see the accompanying
// // file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <chrono>
// #include <memory>
// #include <utility>
// #include <boost/test/unit_test.hpp>
// #include <kth/node.hpp>
// #include "utility.hpp"

// using namespace kth;
// using namespace kth::domain::config;
// using namespace kth::domain::message;
// using namespace kth::node;
// using namespace kth::node::test;

// // Start Test Suite: reservation tests

// // slot
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  slot  construct 42  42", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     size_t const expected = 42;
//     reservation reserve(reserves, expected, 0);
//     REQUIRE(reserve.empty());
// }

// // empty
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  empty  default  true", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     REQUIRE(reserve.empty());
// }

// TEST_CASE("reservation  empty  one hash  false", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     reserve.insert(check42);
//     REQUIRE( ! reserve.empty());
// }

// // size
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  size  default  0", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     REQUIRE(reserve.size() == 0u);
// }

// TEST_CASE("reservation  size  one hash  1", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     reserve.insert(check42);
//     REQUIRE(reserve.size() == 1u);
// }

// TEST_CASE("reservation  size  duplicate hash  1", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     reserve.insert(check42);
//     reserve.insert(check42);
//     REQUIRE(reserve.size() == 1u);
// }

// // stopped
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  stopped  default  false", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     REQUIRE( ! reserve.stopped());
// }

// TEST_CASE("reservation  stopped  one hash  false", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     reserve.insert(check42);
//     REQUIRE( ! reserve.stopped());
// }

// TEST_CASE("reservation  stopped  import last block  true", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     auto const reserve = std::make_shared<reservation>(reserves, 0, 0);
//     auto const message = message_factory(1, check42.hash());
//     auto const& header1 = message->elements()[0];
//     reserve->insert(header1.hash(), 42);
//     auto const block1 = std::make_shared<const block>(block{ header1, {} });
//     reserve->import(block1);
//     REQUIRE(reserve->empty());
//     REQUIRE(reserve->stopped());
// }

// // rate
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  rate  default  defaults", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     auto const rate = reserve.rate();
//     REQUIRE(rate.idle);
//     REQUIRE(rate.events == 0u);
//     REQUIRE(rate.database == 0u);
//     REQUIRE(rate.window == 0u);
// }

// // set_rate
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  set rate  values  expected", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     performance value;
//     value.idle = false;
//     value.events = 1;
//     value.database = 2;
//     value.window = 3;
//     reserve.set_rate(std::move(value));
//     auto const rate = reserve.rate();
//     REQUIRE( ! rate.idle);
//     REQUIRE(rate.events == 1u);
//     REQUIRE(rate.database == 2u);
//     REQUIRE(rate.window == 3u);
// }

// // pending
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  pending  default  true", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     REQUIRE(reserve.pending());
// }

// // set_pending
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  set pending  false true  false true", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     reserve.set_pending(false);
//     REQUIRE( ! reserve.pending());
//     reserve.set_pending(true);
//     REQUIRE(reserve.pending());
// }

// // rate_window
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  rate window  construct 10  30 seconds", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     size_t const expected = 10;
//     reservation_fixture reserve(reserves, 0, expected);
//     auto const window = reserve.rate_window();
//     REQUIRE(window.count() == expected * 1000 * 1000 * 3);
// }

// // reset
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  reset  values  defaults", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);

//     // The timeout cannot be exceeded because the current time is fixed.
//     static const uint32_t timeout = 1;
//     auto const now = std::chrono::high_resolution_clock::now();
//     auto const reserve = std::make_shared<reservation_fixture>(reserves, 0, timeout, now);

//     // Create a history entry.
//     auto const message = message_factory(3, null_hash);
//     reserve->insert(message->elements()[0].hash(), 0);
//     auto const block0 = std::make_shared<const block>(block{ message->elements()[0], {} });
//     auto const block1 = std::make_shared<const block>(block{ message->elements()[1], {} });
//     auto const block2 = std::make_shared<const block>(block{ message->elements()[2], {} });
//     reserve->import(block0);
//     reserve->import(block1);

//     // Idle checks assume minimum_history is set to 3.
//     REQUIRE(reserve->idle());

//     // Set rate.
//     reserve->set_rate({ false, 1, 2, 3 });

//     // Clear rate and history.
//     reserve->reset();

//     // Confirm reset of rate.
//     auto const rate = reserve->rate();
//     REQUIRE(rate.idle);
//     REQUIRE(rate.events == 0u);
//     REQUIRE(rate.database == 0u);
//     REQUIRE(rate.window == 0u);

//     // Confirm clearance of history (non-idle indicated with third history).
//     reserve->import(block2);
//     REQUIRE(reserve->idle());
// }

// // idle
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  idle  default  true", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     REQUIRE(reserve.idle());
// }

// TEST_CASE("reservation  idle  set false  false", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     reserve.set_rate({ false, 1, 2, 3 });
//     REQUIRE( ! reserve.idle());
// }

// // insert
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  insert1  single  size 1 pending", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, false);
//     auto const reserve = std::make_shared<reservation_fixture>(reserves, 0, 0);
//     auto const message = message_factory(1, check42.hash());
//     auto const& header = message->elements()[0];
//     REQUIRE(reserve->empty());
//     reserve->set_pending(false);
//     reserve->insert(checkpoint{ header.hash(), 42 });
//     REQUIRE(reserve->size() == 1u);
//     REQUIRE(reserve->pending());
// }

// // TODO: verify pending.
// TEST_CASE("reservation  insert2  single  size 1 pending", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, false);
//     auto const reserve = std::make_shared<reservation_fixture>(reserves, 0, 0);
//     auto const message = message_factory(1, check42.hash());
//     auto const& header = message->elements()[0];
//     REQUIRE(reserve->empty());
//     reserve->set_pending(false);
//     reserve->insert(header.hash(), 42);
//     REQUIRE(reserve->size() == 1u);
//     REQUIRE(reserve->pending());
// }

// // import
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  import  unsolicitied   empty idle", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     auto const reserve = std::make_shared<reservation>(reserves, 0, 0);
//     auto const message = message_factory(1, check42.hash());
//     auto const& header = message->elements()[0];
//     auto const block1 = std::make_shared<const block>(block{ header, {} });
//     REQUIRE(reserve->idle());
//     reserve->import(block1);
//     REQUIRE(reserve->idle());
//     REQUIRE(reserve->empty());
// }

// TEST_CASE("reservation  import  fail  idle", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, false);
//     auto const reserve = std::make_shared<reservation>(reserves, 0, 0);
//     auto const message = message_factory(1, check42.hash());
//     auto const& header = message->elements()[0];
//     reserve->insert(header.hash(), 42);
//     auto const block1 = std::make_shared<const block>(block{ header, {} });
//     REQUIRE(reserve->idle());
//     reserve->import(block1);
//     REQUIRE(reserve->idle());
// }

// TEST_CASE("reservation  import  three success timeout  idle", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);

//     // If import time is non-zero the zero timeout will exceed and history will not accumulate.
//     static const uint32_t timeout = 0;
//     auto const now = std::chrono::high_resolution_clock::now();
//     auto const reserve = std::make_shared<reservation_fixture>(reserves, 0, timeout, now);
//     auto const message = message_factory(3, null_hash);
//     reserve->insert(message->elements()[0].hash(), 0);
//     reserve->insert(message->elements()[1].hash(), 1);
//     reserve->insert(message->elements()[2].hash(), 2);
//     auto const block0 = std::make_shared<const block>(block{ message->elements()[0], {} });
//     auto const block1 = std::make_shared<const block>(block{ message->elements()[1], {} });
//     auto const block2 = std::make_shared<const block>(block{ message->elements()[2], {} });
//     reserve->import(block0);
//     reserve->import(block1);
//     reserve->import(block2);

//     // Idle checks assume minimum_history is set to 3.
//     REQUIRE(reserve->idle());
// }

// TEST_CASE("reservation  import  three success  not idle", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);

//     // The timeout cannot be exceeded because the current time is fixed.
//     static const uint32_t timeout = 1;
//     auto const now = std::chrono::high_resolution_clock::now();
//     auto const reserve = std::make_shared<reservation_fixture>(reserves, 0, timeout, now);
//     auto const message = message_factory(3, null_hash);
//     reserve->insert(message->elements()[0].hash(), 0);
//     reserve->insert(message->elements()[1].hash(), 1);
//     reserve->insert(message->elements()[2].hash(), 2);
//     auto const block0 = std::make_shared<const block>(block{ message->elements()[0], {} });
//     auto const block1 = std::make_shared<const block>(block{ message->elements()[1], {} });
//     auto const block2 = std::make_shared<const block>(block{ message->elements()[2], {} });

//     // Idle checks assume minimum_history is set to 3.
//     REQUIRE(reserve->idle());
//     reserve->import(block0);
//     REQUIRE(reserve->idle());
//     reserve->import(block1);
//     REQUIRE(reserve->idle());
//     reserve->import(block2);
//     REQUIRE( ! reserve->idle());
// }

// // toggle_partitioned
// //-----------------------------------------------------------------------------

// // see reservations__populate__hashes_empty__partition for positive test.
// TEST_CASE("reservation  toggle partitioned  default  false pending", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     REQUIRE( ! reserve.toggle_partitioned());
//     REQUIRE(reserve.pending());
// }

// // partition
// //-----------------------------------------------------------------------------

// // see reservations__populate__ for positive tests.
// TEST_CASE("reservation  partition  minimal not empty  false unchanged", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     auto const reserve1 = std::make_shared<reservation_fixture>(reserves, 0, 0);
//     auto const reserve2 = std::make_shared<reservation_fixture>(reserves, 1, 0);
//     reserve2->insert(check42);
//     REQUIRE(reserve1->partition(reserve2));
//     REQUIRE(reserve2->size() == 1u);
// }

// // request
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  request  pending  empty not reset", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     reserve.set_rate({ false, 1, 2, 3 });
//     REQUIRE(reserve.pending());

//     // Creates a request with no hashes reserved.
//     auto const result = reserve.request(false);
//     REQUIRE(result.inventories().empty());
//     REQUIRE( ! reserve.pending());

//     // The rate is not reset because the new channel parameter is false.
//     auto const rate = reserve.rate();
//     REQUIRE( ! rate.idle);
//     REQUIRE(rate.events == 1u);
//     REQUIRE(rate.database == 2u);
//     REQUIRE(rate.window == 3u);
// }

// TEST_CASE("reservation  request  new channel pending  size 1 reset", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     auto const message = message_factory(1, null_hash);
//     reserve.insert(message->elements()[0].hash(), 0);
//     reserve.set_rate({ false, 1, 2, 3 });
//     REQUIRE(reserve.pending());

//     // Creates a request with one hash reserved.
//     auto const result = reserve.request(true);
//     REQUIRE(result.inventories().size() == 1u);
//     REQUIRE(result.inventories()[0].hash() == message->elements()[0].hash());
//     REQUIRE( ! reserve.pending());

//     // The rate is reset because the new channel parameter is true.
//     auto const rate = reserve.rate();
//     REQUIRE(rate.idle);
//     REQUIRE(rate.events == 0u);
//     REQUIRE(rate.database == 0u);
//     REQUIRE(rate.window == 0u);
// }

// TEST_CASE("reservation  request  new channel  size 1 reset", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     auto const message = message_factory(1, null_hash);
//     reserve.insert(message->elements()[0].hash(), 0);
//     reserve.set_rate({ false, 1, 2, 3 });
//     reserve.set_pending(false);

//     // Creates a request with one hash reserved.
//     auto const result = reserve.request(true);
//     REQUIRE(result.inventories().size() == 1u);
//     REQUIRE(result.inventories()[0].hash() == message->elements()[0].hash());
//     REQUIRE( ! reserve.pending());

//     // The rate is reset because the new channel parameter is true.
//     auto const rate = reserve.rate();
//     REQUIRE(rate.idle);
//     REQUIRE(rate.events == 0u);
//     REQUIRE(rate.database == 0u);
//     REQUIRE(rate.window == 0u);
// }

// TEST_CASE("reservation  request  three hashes pending  size 3", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     auto const message = message_factory(3, null_hash);
//     reserve.insert(message->elements()[0].hash(), 0);
//     reserve.insert(message->elements()[1].hash(), 1);
//     reserve.insert(message->elements()[2].hash(), 2);
//     REQUIRE(reserve.pending());

//     // Creates a request with 3 hashes reserved.
//     auto const result = reserve.request(false);
//     REQUIRE(result.inventories().size() == 3u);
//     REQUIRE(result.inventories()[0].hash() == message->elements()[0].hash());
//     REQUIRE(result.inventories()[1].hash() == message->elements()[1].hash());
//     REQUIRE(result.inventories()[2].hash() == message->elements()[2].hash());
//     REQUIRE( ! reserve.pending());
// }

// TEST_CASE("reservation  request  one hash  empty", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation_fixture reserve(reserves, 0, 0);
//     auto const message = message_factory(1, null_hash);
//     reserve.insert(message->elements()[0].hash(), 0);
//     reserve.set_pending(false);

//     // Creates an empty request for not new and not pending scneario.
//     auto const result = reserve.request(false);
//     REQUIRE(result.inventories().empty());
//     REQUIRE( ! reserve.pending());
// }

// // expired
// //-----------------------------------------------------------------------------

// TEST_CASE("reservation  expired  default  false", "[reservation tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reservation reserve(reserves, 0, 0);
//     REQUIRE( ! reserve.expired());
// }

// TEST_CASE("reservation  expired  various  expected", "[reservation tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 5;
//     blockchain_fixture blockchain;
//     infrastructure::config::checkpoint::list checkpoints;
//     header_queue hashes(checkpoints);
//     auto const message = message_factory(4, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));
//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();

//     // Simulate the rate summary on each channel by setting it directly.

//     // normalized rate: 5 / (2 - 1) = 5
//     table[0]->set_rate({ false,  5,  1,  2 });

//     // normalized rate: 42 / (42 - 42) = 0
//     // This rate is idle, so values must be excluded in rates computation.
//     table[1]->set_rate({ true,  42, 42, 42 });

//     // normalized rate: 10 / (6 - 1) = 2
//     table[2]->set_rate({ false, 10,  1,  6 });

//     // normalized rate: 3 / (6 - 3) = 1
//     table[3]->set_rate({ false,  3,  3,  6 });

//     // normalized rate: 8 / (5 - 3) = 4
//     table[4]->set_rate({ false,  8,  3,  5 });

//     // see reservations__rates__five_reservations_one_idle__idle_excluded
//     auto const rates2 = reserves.rates();
//     REQUIRE(rates2.active_count == 4u);
//     REQUIRE(rates2.arithmentic_mean == 3.0);

//     // standard deviation: ~ 1.58
//     REQUIRE(rates2.standard_deviation == std::sqrt(2.5));

//     // deviation: 5 - 3 = +2
//     REQUIRE( ! table[0]->expired());

//     // deviation: 0 - 3 = -3
//     REQUIRE(table[1]->expired());

//     // deviation: 2 - 3 = -1
//     REQUIRE( ! table[2]->expired());

//     // deviation: 1 - 3 = -2
//     REQUIRE(table[3]->expired());

//     // deviation: 4 - 3 = +1
//     REQUIRE( ! table[4]->expired());
// }

// // End Test Suite
