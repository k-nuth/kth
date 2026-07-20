// // Copyright (c) 2016-present Knuth Project developers.
// // Distributed under the MIT software license, see the accompanying
// // file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <algorithm>
// #include <memory>
// #include <boost/test/unit_test.hpp>
// #include <kth/node.hpp>
// #include "utility.hpp"

// using namespace kth;
// using namespace kth::domain::config;
// using namespace kth::domain::message;
// using namespace kth::node;
// using namespace kth::node::test;

// // Start Test Suite: reservations tests

// // max_request
// //-----------------------------------------------------------------------------

// TEST_CASE("reservations  max request  default  50000", "[reservations tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     REQUIRE(reserves.max_request() == 50000u);
// }

// TEST_CASE("reservations  set max request  42  42", "[reservations tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     reserves.set_max_request(42);
//     REQUIRE(reserves.max_request() == 42u);
// }

// // import
// //-----------------------------------------------------------------------------

// TEST_CASE("reservations  import  true  true", "[reservations tests]")
// {
//     auto const block_ptr = std::make_shared<const block>();
//     DECLARE_RESERVATIONS(reserves, true);
//     REQUIRE(reserves.import(block_ptr, 42));
// }

// TEST_CASE("reservations  import  false  false", "[reservations tests]")
// {
//     auto const block_ptr = std::make_shared<const block>();
//     DECLARE_RESERVATIONS(reserves, false);
//     REQUIRE( ! reserves.import(block_ptr, 42));
// }

// // table
// //-----------------------------------------------------------------------------

// TEST_CASE("reservations  table  default  empty", "[reservations tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     REQUIRE(reserves.table().empty());
// }

// TEST_CASE("reservations  table  hash 1  size 1 by 1 hashes empty", "[reservations tests]")
// {
//     node::settings settings;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     hashes.initialize(check42);
//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 1u);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[0]->slot() == 0u);
//     REQUIRE(hashes.empty());
// }

// TEST_CASE("reservations  table  hash 4  size 4 by 1 hashes empty", "[reservations tests]")
// {
//     node::settings settings;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(3, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 4u);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);
//     REQUIRE(table[3]->size() == 1u);
//     REQUIRE(table[0]->slot() == 0u);
//     REQUIRE(table[1]->slot() == 1u);
//     REQUIRE(table[2]->slot() == 2u);
//     REQUIRE(table[3]->slot() == 3u);
//     REQUIRE(hashes.empty());
// }

// TEST_CASE("reservations  table  connections 5 hash 46  size 5 by 9 hashes 1", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 5;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(45, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 5u);
//     REQUIRE(table[0]->size() == 9u);
//     REQUIRE(table[1]->size() == 9u);
//     REQUIRE(table[2]->size() == 9u);
//     REQUIRE(table[3]->size() == 9u);
//     REQUIRE(table[4]->size() == 9u);
//     REQUIRE(table[0]->slot() == 0u);
//     REQUIRE(table[1]->slot() == 1u);
//     REQUIRE(table[2]->slot() == 2u);
//     REQUIRE(table[3]->slot() == 3u);
//     REQUIRE(table[4]->slot() == 4u);
//     REQUIRE(hashes.size() == 1u);
// }

// TEST_CASE("reservations  table  hash 42  size 8 by 5 hashes 2", "[reservations tests]")
// {
//     node::settings settings;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(41, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 8u);
//     REQUIRE(table[0]->size() == 5u);
//     REQUIRE(table[1]->size() == 5u);
//     REQUIRE(table[2]->size() == 5u);
//     REQUIRE(table[3]->size() == 5u);
//     REQUIRE(table[4]->size() == 5u);
//     REQUIRE(table[5]->size() == 5u);
//     REQUIRE(table[6]->size() == 5u);
//     REQUIRE(table[7]->size() == 5u);
//     REQUIRE(table[0]->slot() == 0u);
//     REQUIRE(table[1]->slot() == 1u);
//     REQUIRE(table[2]->slot() == 2u);
//     REQUIRE(table[3]->slot() == 3u);
//     REQUIRE(table[4]->slot() == 4u);
//     REQUIRE(table[5]->slot() == 5u);
//     REQUIRE(table[6]->slot() == 6u);
//     REQUIRE(table[7]->slot() == 7u);
//     REQUIRE(hashes.size() == 2u);
// }

// // remove
// //-----------------------------------------------------------------------------

// TEST_CASE("reservations  remove  empty  does not throw", "[reservations tests]")
// {
//     node::settings settings;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     hashes.initialize(check42);
//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 1u);

//     auto const row = table[0];
//     reserves.remove(row);
//     REQUIRE(reserves.table().empty());
//     BOOST_REQUIRE_NO_THROW(reserves.remove(row));
// }

// TEST_CASE("reservations  remove  hash 4  size 3", "[reservations tests]")
// {
//     node::settings settings;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(3, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table1 = reserves.table();
//     REQUIRE(table1.size() == 4u);
//     REQUIRE(hashes.empty());

//     auto const row = table1[2];
//     REQUIRE(row->slot() == 2u);

//     reserves.remove(row);
//     auto const table2 = reserves.table();
//     REQUIRE(table2.size() == 3u);
//     REQUIRE(table2[0]->slot() == 0u);
//     REQUIRE(table2[1]->slot() == 1u);
//     REQUIRE(table2[2]->slot() == 3u);
// }

// // populate
// //-----------------------------------------------------------------------------

// TEST_CASE("reservations  populate  hashes not empty row not empty  no population", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 3;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(9, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 3u);

//     // All rows have three hashes.
//     REQUIRE(table[0]->size() == 3u);
//     REQUIRE(table[1]->size() == 3u);
//     REQUIRE(table[2]->size() == 3u);

//     // The row is not empty so must not cause a reserve.
//     // The row is not empty so must not cause a repartitioning.
//     REQUIRE(hashes.size() == 1u);
//     REQUIRE(reserves.populate(table[1]));
//     REQUIRE(hashes.size() == 1u);

//     // All rows still have three hashes.
//     REQUIRE(table[0]->size() == 3u);
//     REQUIRE(table[1]->size() == 3u);
//     REQUIRE(table[2]->size() == 3u);
// }

// TEST_CASE("reservations  populate  hashes empty row not empty  no population", "[reservations tests]")
// {
//     node::settings settings;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(3, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 4u);

//     // All rows have one hash.
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);
//     REQUIRE(table[3]->size() == 1u);

//     // There are no hashes in reserve.
//     REQUIRE(hashes.empty());

//     // The row is not empty so must not cause a repartitioning.
//     REQUIRE(reserves.populate(table[0]));

//     // Partitions remain unchanged.
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);
//     REQUIRE(table[3]->size() == 1u);
// }

// TEST_CASE("reservations  populate  hashes empty table empty  no population", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 3;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);

//     // Initialize with a known header so we can import its block later.
//     auto const message = message_factory(3, null_hash);
//     auto& elements = message->elements();
//     auto const genesis_header = elements[0];
//     hashes.initialize(genesis_header.hash(), 0);
//     elements.erase(std::find(elements.begin(), elements.end(), elements[0]));
//     REQUIRE(elements.size() == 2u);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 3u);

//     // There are no hashes in reserve.
//     REQUIRE(hashes.empty());

//     // Declare blocks that hash to the allocated headers.
//     // Blocks are evenly distrubuted (every third to each row).
//     auto const block0 = std::make_shared<const block>(block{ genesis_header, {} });
//     auto const block1 = std::make_shared<const block>(block{ elements[0], {} });
//     auto const block2 = std::make_shared<const block>(block{ elements[1], {} });

//     // All rows have one hash.
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);

//     // Remove all rows from the member table.
//     reserves.remove(table[0]);
//     reserves.remove(table[1]);
//     reserves.remove(table[2]);
//     REQUIRE(reserves.table().empty());

//     // Removing a block from the first row of the cached table must result in
//     // one less hash in that row and no partitioning of other rows, since they
//     // are no longer accessible from the member table.
//     table[0]->import(block0);
//     REQUIRE(table[0]->size() == 0u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);
// }

// TEST_CASE("reservations  populate  hashes not empty row emptied  uncapped reserve", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 3;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(7, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 3u);

//     REQUIRE(hashes.size() == 2u);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 2u);

//     auto const block1 = std::make_shared<block>(block{ message->elements()[0], {} });
//     auto const block4 = std::make_shared<block>(block{ message->elements()[3], {} });

//     // The import of two blocks from same row will cause populate to invoke reservation.
//     table[1]->import(block1);
//     REQUIRE(hashes.size() == 2u);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 1u);

//     // The reserve is reduced from 2 to 0 (unlimited by max_request default of 50000).
//     table[1]->import(block4);
//     REQUIRE(hashes.size() == 0u);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 2u);
// }

// TEST_CASE("reservations  populate  hashes not empty row emptied  capped reserve", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 3;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(7, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 3u);

//     REQUIRE(hashes.size() == 2u);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 2u);

//     // Cap reserve at 1 block.
//     reserves.set_max_request(1);

//     auto const block1 = std::make_shared<const block>(block{ message->elements()[0], {} });
//     auto const block4 = std::make_shared<const block>(block{ message->elements()[3], {} });

//     // The import of two blocks from same row will cause populate to invoke reservation.
//     table[1]->import(block1);
//     REQUIRE(hashes.size() == 2u);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 1u);

//     // The reserve is reduced from 2 to 1 (limited by max_request of 1).
//     table[1]->import(block4);
//     REQUIRE(hashes.size() == 1u);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 1u);
// }

// TEST_CASE("reservations  populate  hashes empty rows emptied  partition", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 3;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);

//     // Initialize with a known header so we can import its block later.
//     auto const message = message_factory(9, null_hash);
//     auto& elements = message->elements();
//     auto const genesis_header = elements[0];
//     hashes.initialize(genesis_header.hash(), 0);
//     elements.erase(std::find(elements.begin(), elements.end(), elements[0]));
//     REQUIRE(elements.size() == 8u);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 3u);

//     // There are no hashes in reserve.
//     REQUIRE(hashes.empty());

//     // Declare blocks that hash to the allocated headers.
//     // Blocks are evenly distrubuted (every third to each row).
//     auto const block0 = std::make_shared<const block>(block{ genesis_header, {} });
//     auto const block1 = std::make_shared<const block>(block{ elements[0], {} });
//     auto const block2 = std::make_shared<const block>(block{ elements[1], {} });
//     auto const block3 = std::make_shared<const block>(block{ elements[2], {} });
//     auto const block4 = std::make_shared<const block>(block{ elements[3], {} });
//     auto const block5 = std::make_shared<const block>(block{ elements[4], {} });
//     auto const block6 = std::make_shared<const block>(block{ elements[5], {} });
//     auto const block7 = std::make_shared<const block>(block{ elements[6], {} });
//     auto const block8 = std::make_shared<const block>(block{ elements[7], {} });

//     // This will reset pending on all rows.
//     REQUIRE(table[0]->request(false).inventories().size() == 3u);
//     REQUIRE(table[1]->request(false).inventories().size() == 3u);
//     REQUIRE(table[2]->request(false).inventories().size() == 3u);

//     // A row becomes stopped once empty.
//     REQUIRE( ! table[0]->stopped());
//     REQUIRE( ! table[1]->stopped());
//     REQUIRE( ! table[2]->stopped());

//     // All rows have three hashes.
//     REQUIRE(table[0]->size() == 3u);
//     REQUIRE(table[1]->size() == 3u);
//     REQUIRE(table[2]->size() == 3u);

//     // Remove a block from the first row.
//     table[0]->import(block0);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 3u);
//     REQUIRE(table[2]->size() == 3u);

//     // Remove another block from the first row.
//     table[0]->import(block3);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 3u);
//     REQUIRE(table[2]->size() == 3u);

//     // Removing the last block from the first row results in partitioning of
//     // of the highst row (row 1 winds the tie with row 2 due to ordering).
//     // Half of the row 1 allocation is moved to row 0, rounded up to 2 hashes.
//     table[0]->import(block6);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 3u);
//     REQUIRE(table[1]->toggle_partitioned());
//     REQUIRE( ! table[1]->toggle_partitioned());

//     // The last row has not been modified.
//     REQUIRE(table[0]->request(false).inventories().size() == 2u);
//     REQUIRE(table[1]->request(false).inventories().size() == 1u);
//     REQUIRE(table[2]->request(false).inventories().size() == 0u);

//     // The rows are no longer pending.
//     REQUIRE(table[0]->request(false).inventories().empty());
//     REQUIRE(table[1]->request(false).inventories().empty());
//     REQUIRE(table[2]->request(false).inventories().empty());

//     // Remove another block from the first row (originally from the second).
//     table[0]->import(block1);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 3u);

//     // Remove another block from the first row (originally from the second).
//     table[0]->import(block4);
//     REQUIRE(table[0]->size() == 2u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);
//     REQUIRE(table[2]->toggle_partitioned());
//     REQUIRE( ! table[2]->toggle_partitioned());

//     // The second row has not been modified.
//     REQUIRE(table[0]->request(false).inventories().size() == 2u);
//     REQUIRE(table[1]->request(false).inventories().size() == 0u);
//     REQUIRE(table[2]->request(false).inventories().size() == 1u);

//     // The rows are no longer pending.
//     REQUIRE(table[0]->request(false).inventories().empty());
//     REQUIRE(table[1]->request(false).inventories().empty());
//     REQUIRE(table[2]->request(false).inventories().empty());

//     // Remove another block from the first row (originally from the third).
//     table[0]->import(block2);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 1u);
//     REQUIRE(table[2]->size() == 1u);

//     // Remove another block from the first row (originally from the third).
//     table[0]->import(block5);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 0u);
//     REQUIRE(table[2]->size() == 1u);
//     REQUIRE(table[1]->stopped());
//     REQUIRE( ! table[1]->toggle_partitioned());

//     // The third row has not been modified and the second row is empty.
//     REQUIRE(table[0]->request(false).inventories().size() == 1u);
//     REQUIRE(table[1]->request(false).inventories().size() == 0u);
//     REQUIRE(table[2]->request(false).inventories().size() == 0u);

//     // The rows are no longer pending.
//     REQUIRE(table[0]->request(false).inventories().empty());
//     REQUIRE(table[1]->request(false).inventories().empty());
//     REQUIRE(table[2]->request(false).inventories().empty());

//     // Remove another block from the first row (originally from the second).
//     table[0]->import(block7);
//     REQUIRE(table[0]->size() == 1u);
//     REQUIRE(table[1]->size() == 0u);
//     REQUIRE(table[2]->size() == 0u);
//     REQUIRE(table[2]->stopped());
//     REQUIRE( ! table[2]->toggle_partitioned());

//     // The second row has not been modified and the third row is empty.
//     REQUIRE(table[0]->request(false).inventories().size() == 1u);
//     REQUIRE(table[1]->request(false).inventories().size() == 0u);
//     REQUIRE(table[2]->request(false).inventories().size() == 0u);

//     // The rows are no longer pending.
//     REQUIRE(table[0]->request(false).inventories().empty());
//     REQUIRE(table[1]->request(false).inventories().empty());
//     REQUIRE(table[2]->request(false).inventories().empty());

//     // Remove another block from the first row (originally from the third).
//     table[0]->import(block8);
//     REQUIRE(table[0]->size() == 0u);
//     REQUIRE(table[1]->size() == 0u);
//     REQUIRE(table[2]->size() == 0u);
//     REQUIRE(table[0]->stopped());

//     // The second and third rows have not been modified and the first is empty.
//     REQUIRE(table[0]->request(false).inventories().size() == 0u);
//     REQUIRE(table[1]->request(false).inventories().size() == 0u);
//     REQUIRE(table[2]->request(false).inventories().size() == 0u);

//     // The rows are no longer pending.
//     REQUIRE(table[0]->request(false).inventories().empty());
//     REQUIRE(table[1]->request(false).inventories().empty());
//     REQUIRE(table[2]->request(false).inventories().empty());

//     // We can't test the partition aspect of population directly
//     // because there is no way to reduce the row count to empty.
// REQUIRE(reserves.populate(table[0]));
// }

// // rates
// //-----------------------------------------------------------------------------

// TEST_CASE("reservations  rates  default  zeros", "[reservations tests]")
// {
//     DECLARE_RESERVATIONS(reserves, true);
//     auto const rates = reserves.rates();
//     REQUIRE(rates.active_count == 0u);
//     REQUIRE(rates.arithmentic_mean == 0.0);
//     REQUIRE(rates.standard_deviation == 0.0);
// }

// TEST_CASE("reservations  rates  three reservations same rates  no deviation", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 3;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(2, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();
//     REQUIRE(table.size() == 3u);

//     auto const rates1 = reserves.rates();
//     REQUIRE(rates1.active_count == 0u);
//     REQUIRE(rates1.arithmentic_mean == 0.0);
//     REQUIRE(rates1.standard_deviation == 0.0);

//     // normalized rates: 5 / (2 - 1) = 5
//     performance rate0;
//     rate0.idle = false;
//     rate0.events = 5;
//     rate0.database = 1;
//     rate0.window = 2;
//     performance rate1 = rate0;
//     performance rate2 = rate0;

//     // Simulate the rate summary on each channel by setting it directly.
//     table[0]->set_rate(std::move(rate0));
//     table[1]->set_rate(std::move(rate1));
//     table[2]->set_rate(std::move(rate2));

//     auto const rates2 = reserves.rates();

//     // There are three active (non-idle) rows.
//     REQUIRE(rates2.active_count == 3u);

//     // mean: (5 + 5 + 5) / 3 = 5
//     REQUIRE(rates2.arithmentic_mean == 5.0);

//     // deviations: { 5-5=0, 5-5=0, 5-5=0 }
//     // variance: (0^2 + 0^2 + 0^2) / 3 = 0
//     // standard deviation: sqrt(0)
//     REQUIRE(rates2.standard_deviation == 0.0);
// }

// TEST_CASE("reservations  rates  five reservations one idle  idle excluded", "[reservations tests]")
// {
//     node::settings settings;
//     settings.sync_peers = 5;
//     blockchain_fixture blockchain;
//     header_queue hashes(no_checks);
//     auto const message = message_factory(4, check42.hash());
//     hashes.initialize(check42);
//     REQUIRE(hashes.enqueue(message));

//     reservations reserves(hashes, blockchain, settings);
//     auto const table = reserves.table();

//     // normalized rate: 5 / (2 - 1) = 5
//     performance rate0;
//     rate0.idle = false;
//     rate0.events = 5;
//     rate0.database = 1;
//     rate0.window = 2;

//     // This rate is idle, so values must be excluded in rates computation.
//     performance rate1;
//     rate1.idle = true;
//     rate1.events = 42;
//     rate1.database = 42;
//     rate1.window = 42;

//     // normalized rate: 10 / (6 - 1) = 2
//     performance rate2;
//     rate2.idle = false;
//     rate2.events = 10;
//     rate2.database = 1;
//     rate2.window = 6;

//     // normalized rate: 3 / (6 - 3) = 1
//     performance rate3;
//     rate3.idle = false;
//     rate3.events = 3;
//     rate3.database = 3;
//     rate3.window = 6;

//     // normalized rate: 8 / (5 - 3) = 4
//     performance rate4;
//     rate4.idle = false;
//     rate4.events = 8;
//     rate4.database = 3;
//     rate4.window = 5;

//     // Simulate the rate summary on each channel by setting it directly.
//     table[0]->set_rate(std::move(rate0));
//     table[1]->set_rate(std::move(rate1));
//     table[2]->set_rate(std::move(rate2));
//     table[3]->set_rate(std::move(rate3));
//     table[4]->set_rate(std::move(rate4));

//     auto const rates2 = reserves.rates();

//     // There are three active (non-idle) rows.
//     REQUIRE(rates2.active_count == 4u);

//     // mean: (5 + 2 + 1 + 4) / 4 = 3
//     REQUIRE(rates2.arithmentic_mean == 3.0);

//     // deviations: { 3-5=-2, 3-2=1, 3-1=-2, 3-4=-1 }
//     // variance: ((-2)^2 + 1^2 + 2^2 + (-1)^2) / 4 = 2.5
//     // standard deviation: sqrt(2.5)
//     REQUIRE(rates2.standard_deviation == std::sqrt(2.5));
// }

// // End Test Suite
