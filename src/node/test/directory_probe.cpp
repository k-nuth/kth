// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "child_process_harness.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include <kth/node/configuration.hpp>
#include <kth/node/executor/executor.hpp>

using namespace kth;
using namespace kth::test;

namespace fs = std::filesystem;

// =============================================================================
// Telling "not there" apart from "could not tell" (#675)
// =============================================================================
//
// `std::filesystem::exists(p, ec)` answers two different questions with one
// value, and only one of them is a failure. A path that is not there is an
// ANSWER: `ec` stays clear, because nothing went wrong — the directory just is
// not there yet, which is what every first run looks like. `ec` is set only
// when the question could not be answered at all.
//
// Reading `!exists` as a failure and printing `ec.message()` therefore printed
// the message for errno 0 — `Success` — as the reason for a failure, on the
// first line of every clean first run, and left the branch written for a real
// failure permanently out of reach.
//
// The three answers each get a case below, and the two that are not failures
// also assert that nothing was logged at `error`: getting the return value
// right and still printing the line would fix nothing an operator can see.

namespace {

// Swap the default logger for one that keeps what was written, so a case can
// ask what the probe actually logged rather than what it was supposed to.
struct captured_log {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    std::shared_ptr<spdlog::logger> installed;
    std::shared_ptr<spdlog::logger> previous;

    captured_log()
        : sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(256))
        , installed(std::make_shared<spdlog::logger>("captured", sink))
        , previous(spdlog::default_logger()) {
        installed->set_level(spdlog::level::trace);
        spdlog::set_default_logger(installed);
    }

    ~captured_log() { spdlog::set_default_logger(previous); }

    captured_log(captured_log const&) = delete;
    captured_log& operator=(captured_log const&) = delete;

    [[nodiscard]] size_t errors() const {
        size_t n = 0;
        for (auto const& entry : sink->last_raw()) {
            if (entry.level >= spdlog::level::err) {
                ++n;
            }
        }
        return n;
    }

    [[nodiscard]] std::string text() const {
        std::string all;
        for (auto const& line : sink->last_formatted()) {
            all += line;
        }
        return all;
    }
};

// The log files go in `base`, not in the datadir: opening one creates the
// directory that holds it, and a fixture that creates the datadir it is about
// to ask after would answer its own question.
node::configuration config_for(fs::path const& base, fs::path const& datadir) {
    node::configuration cfg{domain::config::network::regtest};
    cfg.database.directory = datadir;
    cfg.database.db_max_size = 64ULL << 20;
    cfg.network.threads = 1;
    cfg.network.outbound_connections = 0;
    cfg.network.inbound_connections = 0;
    cfg.network.inbound_port = 0;
    cfg.network.debug_file = base / "debug.log";
    cfg.network.error_file = base / "error.log";
    return cfg;
}

} // namespace

TEST_CASE("directory probe: a datadir that is not there is an answer, not a failure",
          "[directory][probe]") {
    // The ordinary first run, and the one the defect fired on. `chain` has
    // never been created; the node goes on to create it and starts normally.
    auto const base = claimed_dir("kth_dirprobe_absent");
    auto const datadir = base / "chain";
    REQUIRE_FALSE(fs::exists(datadir));

    node::executor const host(config_for(base, datadir), false);

    captured_log const log;
    auto const present = host.probe_directory();

    // Answered, and the answer is no.
    REQUIRE(present.has_value());
    CHECK_FALSE(*present);

    // And answering "no" is not an event an operator should be reading about.
    CHECK(log.errors() == 0);

    fs::remove_all(base);
}

TEST_CASE("directory probe: a datadir that is there is reported present, quietly",
          "[directory][probe]") {
    // The other half of the same question, so that a probe which answered
    // "absent" to everything could not pass the case above.
    auto const base = claimed_dir("kth_dirprobe_present");
    auto const datadir = base / "chain";
    REQUIRE(fs::create_directory(datadir));

    node::executor const host(config_for(base, datadir), false);

    captured_log const log;
    auto const present = host.probe_directory();

    REQUIRE(present.has_value());
    CHECK(*present);
    CHECK(log.errors() == 0);

    fs::remove_all(base);
}

TEST_CASE("directory probe: a datadir under a regular file is absent, not a failure",
          "[directory][probe]") {
    // The second path on which `ec` stays clear: the standard folds "a parent
    // that is not a directory" into not-found as well. It reaches the same
    // branch as the case above through a different errno, and it is the shape
    // a mistyped datadir has — so if either one is going to be read as a
    // failure it will be this one.
    auto const base = claimed_dir("kth_dirprobe_notdir");
    std::ofstream blocker(base / "blocked");
    blocker.put('x');
    blocker.close();
    REQUIRE(blocker.good());

    auto const datadir = base / "blocked" / "chain";
    node::executor const host(config_for(base, datadir), false);

    captured_log const log;
    auto const present = host.probe_directory();

    REQUIRE(present.has_value());
    CHECK_FALSE(*present);
    CHECK(log.errors() == 0);

    fs::remove_all(base);
}

#if ! defined(_WIN32)
TEST_CASE("directory probe: a datadir that cannot be queried reports the real reason",
          "[directory][probe]") {
    // The failure the code was written for and never reached. A symlink loop
    // is the one unanswerable path that does not depend on who is running the
    // suite: unlike an unreadable parent, it stays unanswerable for root.
    auto const base = claimed_dir("kth_dirprobe_loop");
    fs::create_symlink(base / "loop_b", base / "loop_a");
    fs::create_symlink(base / "loop_a", base / "loop_b");

    auto const datadir = base / "loop_a" / "chain";
    node::executor const host(config_for(base, datadir), false);

    captured_log const log;
    auto const present = host.probe_directory();

    // Not an answer: the system declined to give one.
    REQUIRE_FALSE(present.has_value());

    // And the reason carried is the system's, not errno 0. Both halves matter:
    // the defect returned a failure too, it just could not say why.
    CHECK(present.error().value() != 0);
    CHECK(present.error() == std::errc::too_many_symbolic_link_levels);

    // Reported once, with that reason in the line.
    CHECK(log.errors() == 1);
    auto const text = log.text();
    CHECK(text.find(present.error().message()) != std::string::npos);
    CHECK(text.find("'Success'") == std::string::npos);

    fs::remove_all(base);
}
#endif // ! defined(_WIN32)

#if ! defined(KTH_DB_READONLY)
TEST_CASE("directory probe: a first run initializes the datadir without an error line",
          "[directory][probe]") {
    // End to end, at the caller the log line came from: the probe answering
    // "absent" has to reach creation, and the whole first run has to leave
    // nothing at `error` behind it. A probe that returned the right value and
    // a caller that logged anyway would pass every case above and still print
    // the line that this is about.
    auto const base = claimed_dir("kth_dirprobe_firstrun");
    auto const datadir = base / "chain";
    REQUIRE_FALSE(fs::exists(datadir));

    node::executor host(config_for(base, datadir), false);

    captured_log const log;
    auto const ec = host.init_directory_if_necessary();

    CHECK_FALSE(ec);
    CHECK(log.errors() == 0);

    // The work actually happened: this passes vacuously if creation was
    // skipped and "no errors" merely means nothing was attempted.
    CHECK(fs::exists(datadir));
    auto const present = host.probe_directory();
    REQUIRE(present.has_value());
    CHECK(*present);

    fs::remove_all(base);
}
#endif // ! defined(KTH_DB_READONLY)
