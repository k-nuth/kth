// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <kth/network/peer_database.hpp>

using namespace kth;
using namespace kth::network;

namespace {

kth::path temp_file() {
    return std::filesystem::temp_directory_path() / "kth_peer_database_test.dat";
}

struct temp_file_guard {
    kth::path path;
    temp_file_guard() : path(temp_file()) {}
    ~temp_file_guard() {
        std::filesystem::remove(path);
    }
};

} // anonymous namespace

TEST_CASE("peer_database basic operations", "[peer_database]") {
    peer_database db;

    infrastructure::config::authority addr1("192.168.1.1:8333");
    infrastructure::config::authority addr2("192.168.1.2:8333");

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

    infrastructure::config::authority addr("192.168.1.1:8333");

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
        infrastructure::config::authority addr2("192.168.1.2:8333");
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

    infrastructure::config::authority addr("192.168.1.1:8333");

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

    infrastructure::config::authority addr1("192.168.1.1:8333");
    infrastructure::config::authority addr2("192.168.1.2:8333");
    infrastructure::config::authority addr3("192.168.1.3:8333");

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

    infrastructure::config::authority addr1("192.168.1.1:8333");
    infrastructure::config::authority addr2("192.168.1.2:8333");
    infrastructure::config::authority addr3("192.168.1.3:8333");

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

    infrastructure::config::authority addr1("192.168.1.1:8333");
    infrastructure::config::authority addr2("192.168.1.2:8333");

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
    auto hosts_path = std::filesystem::temp_directory_path() / "kth_test_hosts.cache";
    auto banlist_path = std::filesystem::temp_directory_path() / "kth_test_banlist.dat";

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
        CHECK(db.exists(infrastructure::config::authority("192.168.1.1:8333")));
        CHECK(db.exists(infrastructure::config::authority("192.168.1.2:8333")));
        CHECK(db.exists(infrastructure::config::authority("192.168.1.3:8333")));

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
        CHECK(db.is_banned(infrastructure::config::authority("192.168.1.1:8333")));
        CHECK(!db.is_banned(infrastructure::config::authority("192.168.1.2:8333")));

        std::filesystem::remove(banlist_path);
    }
}

TEST_CASE("peer_record helper methods", "[peer_record]") {
    peer_record record;
    record.authority = infrastructure::config::authority("192.168.1.1:8333");

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

    infrastructure::config::authority addr1("192.168.1.1:8333");
    infrastructure::config::authority addr2("192.168.1.2:8333");
    infrastructure::config::authority addr3("192.168.1.3:8333");
    infrastructure::config::authority addr4("192.168.1.4:8333");

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
