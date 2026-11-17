// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

#include <kth/network/peer_database.hpp>
#include <kth/domain/message/address.hpp>
#include <kth/domain/message/addrv2.hpp>

using namespace kth;
using namespace kth::network;

namespace {

struct temp_file_guard {
    kth::path directory;
    kth::path path;

    temp_file_guard() {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path()
                / ("kth_peer_database_" + std::to_string(random())
                    + "_" + std::to_string(random()));
            // Exclusive creation also isolates concurrent CTest processes.
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                path = directory / "peers.dat";
                return;
            }
        }
        throw std::runtime_error("Cannot create peer database test directory");
    }

    temp_file_guard(temp_file_guard const&) = delete;
    temp_file_guard& operator=(temp_file_guard const&) = delete;

    ~temp_file_guard() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};

// Test-only helper: `authority` no longer has a throwing string ctor. All
// literals used here are known-good, so `.value()` is safe.
infrastructure::config::authority auth(std::string_view text) {
    return infrastructure::config::authority::parse_from(text).value();
}

} // anonymous namespace

TEST_CASE("peer_database persistence fixtures are isolated", "[peer_database]") {
    temp_file_guard first;
    auto const addr = auth("192.168.1.100:8333");
    {
        peer_database db(first.path);
        (void)db.get_or_create(addr);
        REQUIRE(db.save());
    }
    {
        temp_file_guard second;
        CHECK(first.path != second.path);
        peer_database db(second.path);
        (void)db.load();
        CHECK(db.size() == 0);
    }
    // Destroying another fixture must not remove this fixture's database.
    peer_database reloaded(first.path);
    REQUIRE(reloaded.load());
    CHECK(reloaded.exists(addr));
}

TEST_CASE("peer_database basic operations", "[peer_database]") {
    peer_database db;

    auto addr1 = auth("192.168.1.1:8333");
    auto addr2 = auth("192.168.1.2:8333");

    SECTION("get_or_create creates new record") {
        auto [record, result] = db.get_or_create(addr1);
        CHECK(result == get_result::created);
        CHECK(record.authority == addr1);
        CHECK(record.reputation_score == 0);
        CHECK(!record.is_banned());
        CHECK(db.size() == 1);
    }

    SECTION("get_or_create returns existing record") {
        auto [record1, result1] = db.get_or_create(addr1);
        CHECK(result1 == get_result::created);
        record1.reputation_score = 50;
        CHECK(db.update(record1));

        auto [record2, result2] = db.get_or_create(addr1);
        CHECK(result2 == get_result::existing);
        CHECK(record2.reputation_score == 50);
        CHECK(db.size() == 1);
    }

    SECTION("get returns nullopt for missing") {
        auto record = db.get(addr1);
        CHECK(!record.has_value());
    }

    SECTION("exists checks presence") {
        CHECK(!db.exists(addr1));
        (void)db.get_or_create(addr1);
        CHECK(db.exists(addr1));
    }

    SECTION("remove deletes record") {
        (void)db.get_or_create(addr1);
        CHECK(db.size() == 1);
        CHECK(db.remove(addr1));
        CHECK(db.size() == 0);
        CHECK(!db.exists(addr1));
    }

    SECTION("clear removes all records") {
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        CHECK(db.size() == 2);
        db.clear();
        CHECK(db.size() == 0);
    }
}

TEST_CASE("peer_database ban operations", "[peer_database]") {
    peer_database db;

    auto addr = auth("192.168.1.1:8333");

    SECTION("ban creates record if not exists") {
        db.ban(addr, std::chrono::hours{1}, ban_reason::node_misbehaving);
        CHECK(db.exists(addr));
        CHECK(db.is_banned(addr));
    }

    SECTION("ban updates existing record") {
        auto [record, _] = db.get_or_create(addr);
        record.user_agent = "TestAgent";
        CHECK(db.update(record));

        db.ban(addr, std::chrono::hours{1}, ban_reason::slow_peer);

        auto updated = db.get(addr);
        REQUIRE(updated.has_value());
        CHECK(updated->user_agent == "TestAgent");
        CHECK(updated->is_banned());
        CHECK(updated->ban->reason == ban_reason::slow_peer);
    }

    SECTION("unban clears ban") {
        db.ban(addr, std::chrono::hours{1}, ban_reason::node_misbehaving);
        CHECK(db.is_banned(addr));

        CHECK(db.unban(addr));
        CHECK(!db.is_banned(addr));
        CHECK(db.exists(addr));
    }

    SECTION("is_banned by IP ignores port") {
        db.ban(addr, std::chrono::hours{1}, ban_reason::node_misbehaving);

        std::error_code ec;
        auto ip = ::asio::ip::make_address("192.168.1.1", ec);
        REQUIRE(!ec);

        CHECK(db.is_banned(ip));
    }

    SECTION("get_banned returns all banned") {
        db.ban(addr, std::chrono::hours{1}, ban_reason::node_misbehaving);
        auto addr2 = auth("192.168.1.2:8333");
        (void)db.get_or_create(addr2);

        auto banned = db.get_banned();
        CHECK(banned.size() == 1);
        CHECK(banned[0].authority == addr);
    }

    SECTION("sweep_expired_bans clears old bans") {
        db.ban(addr, std::chrono::seconds{0}, ban_reason::node_misbehaving);

        auto record = db.get(addr);
        REQUIRE(record.has_value());
        CHECK(record->ban.has_value());
        CHECK(record->ban->is_expired());

        db.sweep_expired_bans();

        record = db.get(addr);
        REQUIRE(record.has_value());
        CHECK(!record->ban.has_value());
    }
}

TEST_CASE("peer_database reputation operations", "[peer_database]") {
    peer_database db;

    auto addr = auth("192.168.1.1:8333");

    SECTION("add_misbehavior accumulates score") {
        (void)db.get_or_create(addr);

        CHECK(!db.add_misbehavior(addr, 10, 100));
        auto record = db.get(addr);
        REQUIRE(record.has_value());
        CHECK(record->reputation_score == 10);

        CHECK(!db.add_misbehavior(addr, 40, 100));
        record = db.get(addr);
        CHECK(record->reputation_score == 50);
    }

    SECTION("add_misbehavior returns true at threshold") {
        (void)db.get_or_create(addr);

        CHECK(!db.add_misbehavior(addr, 90, 100));
        CHECK(db.add_misbehavior(addr, 10, 100));

        auto record = db.get(addr);
        CHECK(record->reputation_score == 100);
    }

    SECTION("add_misbehavior creates record if missing") {
        CHECK(db.add_misbehavior(addr, 100, 100));
        CHECK(db.exists(addr));
    }

    SECTION("decay_all_reputation reduces scores") {
        (void)db.get_or_create(addr);
        (void)db.add_misbehavior(addr, 50, 100);

        db.decay_all_reputation(10);

        auto record = db.get(addr);
        CHECK(record->reputation_score == 40);
    }

    SECTION("decay_all_reputation doesn't go below zero") {
        (void)db.get_or_create(addr);
        (void)db.add_misbehavior(addr, 5, 100);

        db.decay_all_reputation(10);

        auto record = db.get(addr);
        CHECK(record->reputation_score == 0);
    }
}

TEST_CASE("peer_database performance operations", "[peer_database]") {
    peer_database db;

    auto addr1 = auth("192.168.1.1:8333");
    auto addr2 = auth("192.168.1.2:8333");
    auto addr3 = auth("192.168.1.3:8333");

    SECTION("record_block_download tracks stats") {
        (void)db.get_or_create(addr1);
        db.record_block_download(addr1, 100, 5000);
        db.record_block_download(addr1, 100, 5000);

        auto record = db.get(addr1);
        REQUIRE(record.has_value());
        CHECK(record->performance.total_blocks == 200);
        CHECK(record->performance.total_time_ms == 10000);
        CHECK(record->performance.sample_count == 2);
        CHECK(record->performance.avg_ms_per_block() == 50.0);
    }

    SECTION("get_median_performance calculates correctly") {
        db.record_block_download(addr1, 100, 1000);
        db.record_block_download(addr2, 100, 5000);
        db.record_block_download(addr3, 100, 10000);

        double median = db.get_median_performance();
        CHECK(median == 50.0);
    }
}

TEST_CASE("peer_database query operations", "[peer_database]") {
    peer_database db;

    auto addr1 = auth("192.168.1.1:8333");
    auto addr2 = auth("192.168.1.2:8333");
    auto addr3 = auth("192.168.1.3:8333");

    SECTION("get_connectable excludes banned") {
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        db.ban(addr2, std::chrono::hours{1}, ban_reason::node_misbehaving);

        auto connectable = db.get_connectable(10);
        CHECK(connectable.size() == 1);
        CHECK(connectable[0] == addr1);
    }

    SECTION("get_connectable excludes bad reputation") {
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        (void)db.add_misbehavior(addr2, 60, 100);

        auto connectable = db.get_connectable(10);
        CHECK(connectable.size() == 1);
        CHECK(connectable[0] == addr1);
    }

    SECTION("get_connectable respects max_count") {
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        (void)db.get_or_create(addr3);

        auto connectable = db.get_connectable(2);
        CHECK(connectable.size() == 2);
    }

    SECTION("get_bad_peers returns banned and high reputation") {
        (void)db.get_or_create(addr1);
        db.ban(addr2, std::chrono::hours{1}, ban_reason::node_misbehaving);
        (void)db.get_or_create(addr3);
        (void)db.add_misbehavior(addr3, 60, 100);

        auto bad = db.get_bad_peers();
        CHECK(bad.size() == 2);
    }
}

TEST_CASE("peer_database JSON persistence", "[peer_database]") {
    temp_file_guard guard;

    auto addr1 = auth("192.168.1.1:8333");
    auto addr2 = auth("192.168.1.2:8333");

    SECTION("save and load roundtrip") {
        {
            peer_database db(guard.path);

            auto [record1, _] = db.get_or_create(addr1);
            record1.user_agent = "TestAgent/1.0";
            record1.services = 1033;
            record1.reputation_score = 25;
            record1.performance.record(500, 25000);
            record1.connection_attempts = 10;
            record1.connection_successes = 8;
            record1.connection_failures = 2;
            CHECK(db.update(record1));

            db.ban(addr2, std::chrono::hours{24}, ban_reason::checkpoint_failed);

            (void)db.save();
        }

        {
            peer_database db(guard.path);
            (void)db.load();

            CHECK(db.size() == 2);

            auto record1 = db.get(addr1);
            REQUIRE(record1.has_value());
            CHECK(record1->user_agent == "TestAgent/1.0");
            CHECK(record1->services == 1033);
            CHECK(record1->reputation_score == 25);
            CHECK(record1->performance.total_blocks == 500);
            CHECK(record1->performance.total_time_ms == 25000);
            CHECK(record1->performance.sample_count == 1);
            CHECK(record1->connection_attempts == 10);
            CHECK(record1->connection_successes == 8);
            CHECK(record1->connection_failures == 2);

            CHECK(db.is_banned(addr2));
            auto record2 = db.get(addr2);
            REQUIRE(record2.has_value());
            CHECK(record2->ban->reason == ban_reason::checkpoint_failed);
        }
    }

    SECTION("handles empty user_agent") {
        {
            peer_database db(guard.path);
            (void)db.get_or_create(addr1);
            (void)db.save();
        }

        {
            peer_database db(guard.path);
            (void)db.load();

            auto record = db.get(addr1);
            REQUIRE(record.has_value());
            CHECK(record->user_agent.empty());
        }
    }

    SECTION("handles user_agent with special characters") {
        {
            peer_database db(guard.path);
            auto [record, _] = db.get_or_create(addr1);
            record.user_agent = "Bitcoin Cash Node \"28.0.1\"";
            CHECK(db.update(record));
            (void)db.save();
        }

        {
            peer_database db(guard.path);
            (void)db.load();

            auto record = db.get(addr1);
            REQUIRE(record.has_value());
            CHECK(record->user_agent == "Bitcoin Cash Node \"28.0.1\"");
        }
    }

    SECTION("JSON format is readable") {
        {
            peer_database db(guard.path);
            auto [record, _] = db.get_or_create(addr1);
            record.user_agent = "BCHN:28.0.1";
            record.services = 1033;
            CHECK(db.update(record));
            (void)db.save();
        }

        // Read raw file content
        std::ifstream file(guard.path);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        // Should contain JSON with expected fields
        CHECK(content.find("\"endpoint\":\"192.168.1.1:8333\"") != std::string::npos);
        CHECK(content.find("\"user_agent\":\"BCHN:28.0.1\"") != std::string::npos);
        CHECK(content.find("\"services\":1033") != std::string::npos);
    }
}

TEST_CASE("peer_database import legacy files", "[peer_database]") {
    temp_file_guard guard;
    auto hosts_path = guard.directory / "hosts.cache";
    auto banlist_path = guard.directory / "banlist.dat";

    std::filesystem::remove(hosts_path);
    std::filesystem::remove(banlist_path);

    SECTION("import_hosts_cache") {
        {
            std::ofstream file(hosts_path);
            file << "192.168.1.1:8333\n";
            file << "192.168.1.2:8333\n";
            file << "invalid\n";
            file << "192.168.1.3:8333\n";
        }

        peer_database db(guard.path);
        size_t imported = db.import_hosts_cache(hosts_path);

        CHECK(imported == 3);
        CHECK(db.size() == 3);
        CHECK(db.exists(auth("192.168.1.1:8333")));
        CHECK(db.exists(auth("192.168.1.2:8333")));
        CHECK(db.exists(auth("192.168.1.3:8333")));

        std::filesystem::remove(hosts_path);
    }

    SECTION("import_banlist legacy format") {
        auto now = std::chrono::system_clock::now();
        auto future = now + std::chrono::hours{24};
        auto past = now - std::chrono::hours{24};

        auto epoch = [](auto tp) {
            return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
        };

        {
            std::ofstream file(banlist_path);
            file << "# Test banlist\n";
            file << "192.168.1.1 " << epoch(now) << " " << epoch(future) << " 1\n";
            file << "192.168.1.2 " << epoch(now) << " " << epoch(past) << " 1\n";
        }

        peer_database db(guard.path);
        size_t imported = db.import_banlist(banlist_path);

        CHECK(imported == 1);
        CHECK(db.is_banned(auth("192.168.1.1:8333")));
        CHECK(!db.is_banned(auth("192.168.1.2:8333")));

        std::filesystem::remove(banlist_path);
    }
}

TEST_CASE("peer_record helper methods", "[peer_record]") {
    peer_record record;
    record.authority = auth("192.168.1.1:8333");

    SECTION("is_banned checks expiration") {
        CHECK(!record.is_banned());

        record.set_banned(std::chrono::hours{1}, ban_reason::node_misbehaving);
        CHECK(record.is_banned());

        record.set_banned(std::chrono::seconds{0}, ban_reason::node_misbehaving);
        CHECK(!record.is_banned());
    }

    SECTION("is_bad_for_sync checks reputation") {
        CHECK(!record.is_bad_for_sync());

        record.reputation_score = 49;
        CHECK(!record.is_bad_for_sync());

        record.reputation_score = 50;
        CHECK(record.is_bad_for_sync());
    }

    SECTION("success_rate calculates correctly") {
        CHECK(record.success_rate() == 0.0);

        record.connection_attempts = 10;
        record.connection_successes = 8;
        CHECK(record.success_rate() == 0.8);
    }

    SECTION("add_misbehavior returns true at threshold") {
        CHECK(!record.add_misbehavior(50, 100));
        CHECK(record.reputation_score == 50);

        CHECK(record.add_misbehavior(50, 100));
        CHECK(record.reputation_score == 100);
    }

    SECTION("decay_reputation doesn't go below zero") {
        record.reputation_score = 5;
        record.decay_reputation(10);
        CHECK(record.reputation_score == 0);
    }
}

TEST_CASE("ban_reason to_string", "[peer_record]") {
    CHECK(to_string(ban_reason::unknown) == "unknown");
    CHECK(to_string(ban_reason::node_misbehaving) == "misbehaving");
    CHECK(to_string(ban_reason::manually_added) == "manually added");
    CHECK(to_string(ban_reason::checkpoint_failed) == "checkpoint failed");
    CHECK(to_string(ban_reason::slow_peer) == "slow peer");
}

TEST_CASE("peer_database capacity invariant", "[peer_database]") {
    // Create database with small capacity for testing
    peer_database db({}, 3);

    auto addr1 = auth("192.168.1.1:8333");
    auto addr2 = auth("192.168.1.2:8333");
    auto addr3 = auth("192.168.1.3:8333");
    auto addr4 = auth("192.168.1.4:8333");

    SECTION("get_or_create respects capacity") {
        auto [r1, res1] = db.get_or_create(addr1);
        CHECK(res1 == get_result::created);

        auto [r2, res2] = db.get_or_create(addr2);
        CHECK(res2 == get_result::created);

        auto [r3, res3] = db.get_or_create(addr3);
        CHECK(res3 == get_result::created);

        CHECK(db.size() == 3);

        // At capacity - should return created_not_stored
        auto [r4, res4] = db.get_or_create(addr4);
        CHECK(res4 == get_result::created_not_stored);
        CHECK(db.size() == 3);  // Size unchanged
        CHECK(!db.exists(addr4));  // Not in database
    }

    SECTION("update does not insert non-existent records") {
        // Create a record manually (not in database)
        peer_record record;
        record.authority = addr1;
        record.ip = normalized_address(addr1);
        record.user_agent = "TestAgent";

        // update() should return false and not insert
        CHECK(!db.update(record));
        CHECK(db.size() == 0);
        CHECK(!db.exists(addr1));
    }

    SECTION("update only modifies existing records") {
        auto [record, result] = db.get_or_create(addr1);
        CHECK(result == get_result::created);

        record.user_agent = "Modified";
        CHECK(db.update(record));

        auto updated = db.get(addr1);
        REQUIRE(updated.has_value());
        CHECK(updated->user_agent == "Modified");
    }

    SECTION("capacity is never exceeded by any operation") {
        // Fill to capacity
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        (void)db.get_or_create(addr3);
        CHECK(db.size() == 3);

        // Try various operations that could potentially insert

        // 1. get_or_create at capacity
        auto [r, res] = db.get_or_create(addr4);
        CHECK(res == get_result::created_not_stored);
        CHECK(db.size() == 3);

        // 2. update non-existent at capacity
        peer_record new_record;
        new_record.authority = addr4;
        new_record.ip = normalized_address(addr4);
        CHECK(!db.update(new_record));
        CHECK(db.size() == 3);

        // 3. ban non-existent at capacity
        db.ban(addr4, std::chrono::hours{1}, ban_reason::node_misbehaving);
        CHECK(db.size() == 3);  // Should not insert

        // 4. add_misbehavior non-existent at capacity
        (void)db.add_misbehavior(addr4, 50);
        CHECK(db.size() == 3);  // Should not insert

        // 5. record_block_download non-existent at capacity
        db.record_block_download(addr4, 100, 5000);
        CHECK(db.size() == 3);  // Should not insert
    }

    SECTION("existing records can still be modified at capacity") {
        // Fill to capacity
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        (void)db.get_or_create(addr3);

        // Modify existing record
        auto [record, result] = db.get_or_create(addr1);
        CHECK(result == get_result::existing);
        record.user_agent = "Updated";
        CHECK(db.update(record));

        // Ban existing record
        db.ban(addr2, std::chrono::hours{1}, ban_reason::slow_peer);
        CHECK(db.is_banned(addr2));

        // Add misbehavior to existing
        (void)db.add_misbehavior(addr3, 50);
        auto r3 = db.get(addr3);
        CHECK(r3->reputation_score == 50);

        CHECK(db.size() == 3);
    }

    SECTION("removing records frees capacity") {
        (void)db.get_or_create(addr1);
        (void)db.get_or_create(addr2);
        (void)db.get_or_create(addr3);
        CHECK(db.size() == 3);

        // At capacity
        auto [r4_before, res_before] = db.get_or_create(addr4);
        CHECK(res_before == get_result::created_not_stored);

        // Remove one
        CHECK(db.remove(addr1));
        CHECK(db.size() == 2);

        // Now we can add
        auto [r4_after, res_after] = db.get_or_create(addr4);
        CHECK(res_after == get_result::created);
        CHECK(db.size() == 3);
    }
}

TEST_CASE("peer_database retains overlay addrv2 addresses across persistence", "[peer_database][addrv2][i2p]") {
    using namespace domain::message;
    auto const timestamp = uint32_t(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    data_chunk bytes(32, 0x42);
    auto const i2p = addrv2_entry::create(timestamp, 1, bip155_network::i2p, bytes, 0);
    auto const onion = addrv2_entry::create(timestamp, 1, bip155_network::torv3, bytes, 8333);
    REQUIRE(i2p);
    REQUIRE(onion);
    auto const i2p_address = i2p->to_network_address();
    auto const onion_address = onion->to_network_address();
    REQUIRE(i2p_address);
    REQUIRE(onion_address);
    auto const i2p_auth = infrastructure::config::authority(*i2p_address);
    auto const onion_auth = infrastructure::config::authority(*onion_address);
    temp_file_guard guard;
    {
        peer_database db(guard.path);
        CHECK(db.store_address(*i2p_address));
        CHECK(db.store_address(*onion_address));
        auto updated = *i2p_address;
        updated.set_services(5);
        updated.set_timestamp(timestamp + 1);
        CHECK( ! db.store_address(updated));
        REQUIRE(db.save());
    }
    {
        // An old IP-only record still loads alongside the overlay records.
        std::ofstream file(guard.path, std::ios::app);
        REQUIRE(file.is_open());
        file << "{\"address\":\"8.8.8.8:8333\",\"services\":1}\n";
        file << "{\"endpoint\":\"9.9.9.9:0\"}\n";
    }
    peer_database db(guard.path);
    REQUIRE(db.load());
    CHECK(db.size() == 3);
    CHECK(db.exists(auth("8.8.8.8:8333")));
    CHECK( ! db.exists(auth("9.9.9.9:0")));
    auto const restored_i2p = db.get(i2p_auth);
    auto const restored_onion = db.get(onion_auth);
    REQUIRE(restored_i2p);
    REQUIRE(restored_onion);
    CHECK(restored_i2p->services == 5);
    CHECK(restored_onion->services == 1);
    CHECK(restored_i2p->last_active == std::chrono::system_clock::time_point(std::chrono::seconds(timestamp + 1)));
    auto const sent_i2p = addrv2_entry::from_network_address(restored_i2p->authority.to_network_address());
    auto const sent_onion = addrv2_entry::from_network_address(restored_onion->authority.to_network_address());
    CHECK(sent_i2p.network() == bip155_network::i2p);
    CHECK(sent_i2p.addr() == bytes);
    CHECK(sent_i2p.port() == 0);
    CHECK(sent_onion.network() == bip155_network::torv3);
    CHECK(sent_onion.addr() == bytes);
    CHECK(sent_onion.port() == 8333);
    auto const legacy = address::create({restored_i2p->authority.to_network_address(),
        restored_onion->authority.to_network_address(), auth("8.8.8.8:8333").to_network_address()});
    REQUIRE(legacy);
    REQUIRE(legacy->addresses().size() == 1);
    CHECK( ! legacy->addresses().front().is_overlay());
}

TEST_CASE("peer_database overlay reputation and bans use the network address", "[peer_database][i2p]") {
    using namespace domain::message;
    auto const i2p = addrv2_entry::create(0, 1, bip155_network::i2p, data_chunk(32, 1), 0);
    auto const other = addrv2_entry::create(0, 1, bip155_network::i2p, data_chunk(32, 2), 0);
    auto const onion = addrv2_entry::create(0, 1, bip155_network::torv3, data_chunk(32, 1), 8333);
    REQUIRE(i2p);
    REQUIRE(other);
    REQUIRE(onion);
    auto const i2p_auth = infrastructure::config::authority(*i2p->to_network_address());
    auto const other_auth = infrastructure::config::authority(*other->to_network_address());
    auto const onion_auth = infrastructure::config::authority(*onion->to_network_address());
    auto const other_onion_port = infrastructure::config::authority::parse_from(onion_auth.to_hostname(), 18333);
    REQUIRE(other_onion_port);
    temp_file_guard guard;
    {
        peer_database db(guard.path);
        CHECK( ! db.add_misbehavior(i2p_auth, 25));
        CHECK(db.add_misbehavior(i2p_auth, 75));
        CHECK(db.is_banned(i2p_auth));
        CHECK( ! db.store_address(*i2p->to_network_address()));
        CHECK( ! db.is_banned(other_auth));
        CHECK( ! db.is_banned(onion_auth));
        CHECK( ! db.is_banned(::asio::ip::make_address("0.0.0.0")));
        db.ban(other_auth, std::chrono::hours(1));
        CHECK(db.is_banned(other_auth));
        db.ban(onion_auth, std::chrono::hours(1));
        CHECK(db.is_banned(*other_onion_port));
        REQUIRE(db.save());
    }
    peer_database db(guard.path);
    REQUIRE(db.load());
    CHECK(db.is_banned(i2p_auth));
    REQUIRE(db.get(i2p_auth));
    CHECK(db.get(i2p_auth)->reputation_score == 100);
    CHECK(db.unban(i2p_auth));
    CHECK( ! db.unban(i2p_auth));
    CHECK( ! db.is_banned(i2p_auth));
    CHECK(db.is_banned(other_auth));
    CHECK(db.is_banned(onion_auth));
    db.record_block_download(i2p_auth, 2, 1200);
    CHECK(db.get_peer_speed(i2p_auth) == 600.0);
    CHECK(db.is_slow_peer(i2p_auth, 500.0, 1));
    CHECK(db.get_peer_speed(other_auth) == 0.0);
    CHECK(db.remove(i2p_auth));
    CHECK( ! db.exists(i2p_auth));
    CHECK(db.exists(other_auth));
    CHECK( ! db.unban(i2p_auth));

    peer_database unwritable(guard.directory / "missing" / "peers.dat");
    unwritable.ban(i2p_auth, std::chrono::hours(1));
    CHECK(unwritable.unban(i2p_auth));
    CHECK( ! unwritable.is_banned(i2p_auth));
}

TEST_CASE("peer_database filters unreachable overlays before selecting peers", "[peer_database][i2p]") {
    using namespace domain::message;
    auto const i2p = addrv2_entry::create(0, 1, bip155_network::i2p, data_chunk(32, 1), 0);
    auto const onion = addrv2_entry::create(0, 1, bip155_network::torv3, data_chunk(32, 1), 8333);
    REQUIRE(i2p);
    REQUIRE(onion);
    auto const i2p_address = *i2p->to_network_address();
    auto const onion_address = *onion->to_network_address();
    auto const i2p_auth = infrastructure::config::authority(i2p_address);
    peer_database db;
    REQUIRE(db.store_address(i2p_address));
    REQUIRE(db.store_address(onion_address));
    auto const i2p_only = [](infrastructure::config::authority const& address) { return address.is_i2p(); };
    auto const none = [](infrastructure::config::authority const&) { return false; };
    auto const connectable = db.get_connectable(1, i2p_only);
    REQUIRE(connectable.size() == 1);
    CHECK(connectable.front() == i2p_auth);
    domain::message::network_address selected;
    CHECK(db.fetch_address(selected, i2p_only) == error::success);
    CHECK(infrastructure::config::authority(selected) == i2p_auth);
    CHECK(db.get_connectable(1, none).empty());
    CHECK(db.fetch_address(selected, none) == error::not_found);
    CHECK(db.size() == 2);

    auto bad_ip = auth("8.8.8.8:0").to_network_address();
    bad_ip.set_services(1);
    CHECK( ! db.store_address(bad_ip));
    auto bad_onion = onion_address;
    bad_onion.set_port(0);
    CHECK( ! db.store_address(bad_onion));
    peer_database invalid;
    (void)invalid.get_or_create(infrastructure::config::authority(bad_ip));
    (void)invalid.get_or_create(infrastructure::config::authority(bad_onion));
    CHECK(invalid.get_connectable(100).empty());
    CHECK(invalid.fetch_address(selected) == error::not_found);
    CHECK(db.remove_address(i2p_address));
    CHECK(db.get_connectable(1, i2p_only).empty());
    auto future_i2p = i2p_address;
    future_i2p.set_port(8333);
    CHECK( ! db.store_address(future_i2p));
    (void)db.get_or_create(infrastructure::config::authority(future_i2p));
    CHECK(db.get_connectable(1, i2p_only).empty());
    CHECK(db.fetch_address(selected, i2p_only) == error::not_found);
}

TEST_CASE("peer_database keeps IPv4 mapped normalization with overlay keys", "[peer_database][i2p]") {
    auto const v4 = auth("8.8.8.8:8333");
    auto const mapped = auth("[::ffff:8.8.8.8]:18333");
    peer_database db;
    (void)db.get_or_create(v4);
    CHECK(db.get(mapped).has_value());
    CHECK(db.exists(mapped));
    db.ban(mapped, std::chrono::hours(1));
    CHECK(db.is_banned(v4));
    CHECK(db.size() == 1);
}
