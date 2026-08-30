// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include "child_process_harness.hpp"

#include <filesystem>
#include <fstream>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <fcntl.h>
#include <sys/param.h>
#include <unistd.h>
#endif

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

// Files under `dir` that this process still has open, read off its own
// descriptor table where the platform exposes one; nothing where it does not.
// On Windows a file that is open cannot be removed, so whatever still holds one
// when its directory goes would make that removal fail there — and on the
// platforms that do allow it, this is how the holder is seen.
std::optional<std::vector<std::string>> open_under(fs::path const& dir) {
    auto const root = fs::weakly_canonical(dir).string();
    auto inside = [&](std::string path) {
        auto const deleted = std::string(" (deleted)");
        if (path.size() > deleted.size() && path.ends_with(deleted)) {
            path.resize(path.size() - deleted.size());
        }
        return path == root || path.starts_with(root + "/");
    };
    std::vector<std::string> open;
#if defined(__linux__)
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator("/proc/self/fd", ec)) {
        std::error_code link_ec;
        auto const target = fs::read_symlink(entry.path(), link_ec);
        if ( ! link_ec && inside(target.string())) {
            open.push_back(target.string());
        }
    }
    if (ec) return std::nullopt;
    return open;
#elif defined(__APPLE__)
    for (int fd = 0, end = getdtablesize(); fd < end; ++fd) {
        char path[MAXPATHLEN];
        if (fcntl(fd, F_GETPATH, path) != -1 && inside(path)) {
            open.push_back(path);
        }
    }
    return open;
#else
    (void)inside;
    return std::nullopt;
#endif
}

// Remove a case's directory, having checked that nothing in this process still
// holds a file in it open. A failed removal is reported, not ignored.
void remove_case_dir(fs::path const& dir) {
    if (auto const open = open_under(dir)) {
        for (auto const& path : *open) {
            UNSCOPED_INFO("still open: " << path);
        }
        CHECK(open->empty());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    INFO("removing " << dir.string() << ": " << ec.message());
    CHECK_FALSE(ec);
}

// The logging registry as a case found it, put back when the case is over, on
// every exit, a failed REQUIRE included.
//
// Constructing an executor makes a logger writing to `base / "debug.log"` the
// default. The registry also keeps every logger it is given by name, past its
// time as the default, and captured_log keeps the one it replaces in order to
// put it back. Each of those holds the file open. Putting back only the default
// would leave the registry's copy, so the whole registry is put back: what the
// case added is dropped, what it replaced is registered again, and the default
// is the one that was there.
class registry_restore {
public:
    registry_restore()
        : default_(spdlog::default_logger())
        , loggers_(registered())
    {}

    ~registry_restore() {
        for (auto const& [name, logger] : registered()) {
            auto const found = loggers_.find(name);
            if (found == loggers_.end() || found->second != logger) {
                spdlog::drop(name);
            }
        }
        for (auto const& [name, logger] : loggers_) {
            if ( ! spdlog::get(name)) {
                spdlog::register_logger(logger);
            }
        }
        spdlog::set_default_logger(default_);
    }

    registry_restore(registry_restore const&) = delete;
    registry_restore& operator=(registry_restore const&) = delete;

private:
    using by_name = std::map<std::string, std::shared_ptr<spdlog::logger>>;

    static by_name registered() {
        by_name out;
        spdlog::apply_all([&](std::shared_ptr<spdlog::logger> logger) {
            out.emplace(logger->name(), std::move(logger));
        });
        return out;
    }

    std::shared_ptr<spdlog::logger> default_;
    by_name loggers_;
};

// The directory a case runs in: claimed when it is constructed, removed when it
// is destroyed. Declared before anything that writes into it, so on every exit
// it goes last, after all of that is gone. On the way out of a failed REQUIRE
// the failure has already been reported, and the removal is only cleanup.
class case_dir {
public:
    explicit case_dir(std::string_view stem)
        : path_(claimed_dir(stem))
    {}

    ~case_dir() {
        if (std::uncaught_exceptions() > 0) {
            std::error_code ignored;
            fs::remove_all(path_, ignored);
            return;
        }
        remove_case_dir(path_);
    }

    case_dir(case_dir const&) = delete;
    case_dir& operator=(case_dir const&) = delete;

    [[nodiscard]] fs::path const& path() const { return path_; }

private:
    fs::path path_;
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

namespace {

// What the lifetime case below throws when it is asked to fail at its very
// end, where a REQUIRE failing would leave it.
struct lifetime_case_failed {};

// What every case below relies on to remove its directory: once the case is
// over, nothing holds the logger the executor opened in it, and the logging
// that was there before is there again. Seen through ownership, which every
// platform has, rather than through what Windows does with a file that is
// still open.
void lifetime_case(bool fail_at_end) {
    // The case's own registration is process-wide too: it is undone on the
    // way out like everything else, a failure included, by a restore made
    // before it.
    registry_restore const restore;
    auto const unrelated = std::make_shared<spdlog::logger>("kth_dirprobe_unrelated");
    spdlog::register_logger(unrelated);
    auto const before = spdlog::default_logger();
    // Whatever the run has registered under the capture's name before this
    // case — nothing, or what another capture left — is what must be there
    // after it. The capture this case makes is its own, and has to be let go.
    auto const captured_before = spdlog::get("captured");

    std::weak_ptr<spdlog::logger> file_logger;
    std::weak_ptr<spdlog::logger> own_capture;
    fs::path removed;
    {
        case_dir const dir{"kth_dirprobe_lifetime"};
        registry_restore const logging;
        auto const& base = dir.path();
        removed = base;

        node::executor const host(config_for(base, base / "chain"), false);
        file_logger = spdlog::default_logger();
        REQUIRE(file_logger.lock() != before);       // the executor did install one

        captured_log const log;
        own_capture = log.installed;
        spdlog::error("[test] written while captured");
        CHECK(log.errors() == 1);                     // captured, not lost
    }

    CHECK(file_logger.expired());
    CHECK_FALSE(fs::exists(removed));
    CHECK(spdlog::default_logger() == before);
    CHECK(spdlog::get("kth_dirprobe_unrelated") == unrelated);
    CHECK(own_capture.expired());
    CHECK(spdlog::get("captured") == captured_before);

    // The same on the way out of a failure: what a failed REQUIRE throws
    // unwinds through the same destructors.
    std::weak_ptr<spdlog::logger> failed_logger;
    std::weak_ptr<spdlog::logger> failed_capture;
    fs::path failed_dir;
    struct case_failed {};
    try {
        case_dir const dir{"kth_dirprobe_lifetime_failed"};
        registry_restore const logging;
        failed_dir = dir.path();
        node::executor const host(config_for(dir.path(), dir.path() / "chain"), false);
        failed_logger = spdlog::default_logger();
        captured_log const log;
        failed_capture = log.installed;
        throw case_failed{};
    } catch (case_failed const&) {}

    REQUIRE_FALSE(failed_dir.empty());
    CHECK(failed_logger.expired());
    CHECK(failed_capture.expired());
    CHECK_FALSE(fs::exists(failed_dir));
    CHECK(spdlog::default_logger() == before);
    CHECK(spdlog::get("captured") == captured_before);

    if (fail_at_end) {
        throw lifetime_case_failed{};
    }
}

} // namespace

TEST_CASE("directory probe: a case lets go of its datadir's log, and of nothing else",
          "[directory][probe][lifetime]") {
    auto const before = spdlog::default_logger();
    lifetime_case(false);

    // And its own registration did not outlive it either.
    CHECK(spdlog::get("kth_dirprobe_unrelated") == nullptr);
    CHECK(spdlog::default_logger() == before);
}

TEST_CASE("directory probe: a case that fails at its end still leaves logging as it found it",
          "[directory][probe][lifetime]") {
    // The same case, left by an exception after everything it checks, which
    // is where a REQUIRE failing would leave it. Whatever it registered in the
    // process-wide logging registry must not outlive it. This case keeps a
    // restore of its own, so that a leak seen here goes no further.
    registry_restore const guard;
    auto const before = spdlog::default_logger();
    auto const captured_before = spdlog::get("captured");
    REQUIRE(spdlog::get("kth_dirprobe_unrelated") == nullptr);

    bool failed = false;
    try {
        lifetime_case(true);
    } catch (lifetime_case_failed const&) {
        failed = true;
    }
    REQUIRE(failed);                                  // it was left by the exception

    CHECK(spdlog::get("kth_dirprobe_unrelated") == nullptr);
    CHECK(spdlog::get("captured") == captured_before);
    CHECK(spdlog::default_logger() == before);
}

TEST_CASE("directory probe: a case leaves loggers registered before it as it found them",
          "[directory][probe][lifetime]") {
    // The registry is process-wide, and other cases in the same run leave
    // their own loggers in it: a capture elsewhere that installs a logger
    // named "captured" and later puts the default back leaves that logger
    // registered. The lifetime case has to hand back that state, not an empty
    // one — with a "captured" logger merely registered before it, and with
    // one that is the default when it begins. Both ways out are taken. This
    // case keeps a restore of its own, so what it sets up goes no further.
    for (bool as_default : {false, true}) {
        for (bool fail_at_end : {false, true}) {
            INFO((as_default ? "registered and the default" : "registered"));
            INFO((fail_at_end ? "left by an exception" : "left normally"));
            registry_restore const guard;
            auto const earlier = std::make_shared<spdlog::logger>("captured");
            auto const outer_default = spdlog::default_logger();
            spdlog::set_default_logger(earlier);
            if ( ! as_default) {
                spdlog::set_default_logger(outer_default);
            }
            auto const before = spdlog::default_logger();

            bool failed = false;
            try {
                lifetime_case(fail_at_end);
            } catch (lifetime_case_failed const&) {
                failed = true;
            }
            REQUIRE(failed == fail_at_end);

            CHECK(spdlog::get("captured") == earlier);
            CHECK(spdlog::default_logger() == before);
            CHECK(spdlog::get("kth_dirprobe_unrelated") == nullptr);
        }
    }
}

TEST_CASE("directory probe: a datadir that is not there is an answer, not a failure",
          "[directory][probe]") {
    // The ordinary first run, and the one the defect fired on. `chain` has
    // never been created; the node goes on to create it and starts normally.
    case_dir const dir{"kth_dirprobe_absent"};
    registry_restore const logging;
    auto const& base = dir.path();
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
}

TEST_CASE("directory probe: a datadir that is there is reported present, quietly",
          "[directory][probe]") {
    // The other half of the same question, so that a probe which answered
    // "absent" to everything could not pass the case above.
    case_dir const dir{"kth_dirprobe_present"};
    registry_restore const logging;
    auto const& base = dir.path();
    auto const datadir = base / "chain";
    REQUIRE(fs::create_directory(datadir));

    node::executor const host(config_for(base, datadir), false);

    captured_log const log;
    auto const present = host.probe_directory();

    REQUIRE(present.has_value());
    CHECK(*present);
    CHECK(log.errors() == 0);
}

TEST_CASE("directory probe: a datadir under a regular file is absent, not a failure",
          "[directory][probe]") {
    // The second path on which `ec` stays clear: the standard folds "a parent
    // that is not a directory" into not-found as well. It reaches the same
    // branch as the case above through a different errno, and it is the shape
    // a mistyped datadir has — so if either one is going to be read as a
    // failure it will be this one.
    case_dir const dir{"kth_dirprobe_notdir"};
    registry_restore const logging;
    auto const& base = dir.path();
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
}

TEST_CASE("directory probe: a regular file at the datadir path is not the datadir",
          "[directory][probe]") {
    // `exists` says yes to a regular file, and being there is not the whole
    // question: a file at the datadir path is not a datadir. Answering
    // "present" would let the node go on to open a database inside a path that
    // is a file, and fail much further from the cause.
    case_dir const dir{"kth_dirprobe_isfile"};
    registry_restore const logging;
    auto const& base = dir.path();
    auto const datadir = base / "chain";
    {
        std::ofstream occupied(datadir);
        occupied.put('x');
        occupied.close();
        REQUIRE(occupied.good());
    }
    REQUIRE(fs::exists(datadir));
    REQUIRE(fs::is_regular_file(datadir));

    node::executor const host(config_for(base, datadir), false);

    captured_log const log;
    auto const present = host.probe_directory();

    // Answered — the query worked — and the answer is that the datadir is not
    // there. Which is not a failure either, so still nothing is logged.
    REQUIRE(present.has_value());
    CHECK_FALSE(*present);
    CHECK(log.errors() == 0);
}

#if ! defined(KTH_DB_READONLY)
TEST_CASE("directory probe: a datadir path occupied by a file fails where the cause is",
          "[directory][probe]") {
    // The other half: reporting it absent hands the decision to creation,
    // which is where a path occupied by a file is discovered and named. One
    // error line, carrying the system's reason.
    case_dir const dir{"kth_dirprobe_isfile_e2e"};
    registry_restore const logging;
    auto const& base = dir.path();
    auto const datadir = base / "chain";
    {
        std::ofstream occupied(datadir);
        occupied.put('x');
        occupied.close();
        REQUIRE(occupied.good());
    }

    node::executor host(config_for(base, datadir), false);

    captured_log const log;
    auto const ec = host.init_directory_if_necessary();

    // It fails, and it fails saying what is wrong rather than 'Success'.
    CHECK(ec);
    CHECK(log.errors() == 1);
    auto const text = log.text();
    CHECK(text.find("'Success'") == std::string::npos);
    CHECK(text.find(ec.message()) != std::string::npos);
}
#endif // ! defined(KTH_DB_READONLY)

#if ! defined(_WIN32)
TEST_CASE("directory probe: a datadir that cannot be queried reports the real reason",
          "[directory][probe]") {
    // The failure the code was written for and never reached. A symlink loop
    // is the one unanswerable path that does not depend on who is running the
    // suite: unlike an unreadable parent, it stays unanswerable for root.
    case_dir const dir{"kth_dirprobe_loop"};
    registry_restore const logging;
    auto const& base = dir.path();
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
    case_dir const dir{"kth_dirprobe_firstrun"};
    registry_restore const logging;
    auto const& base = dir.path();
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
}

TEST_CASE("directory probe: a database that cannot be created fails the first run",
          "[directory][probe]") {
    // Creating the directory and creating the database in it are two stages,
    // and only the first one speaks `error_code`. A database that cannot be
    // created leaves that code clear, and a clear code is what success looks
    // like to the caller: the node would go on to start on a datadir with
    // nothing in it, after an error line naming 'Success'.
    //
    // A map larger than the address space is a database that cannot be
    // created for anyone running the suite, root included, and it is refused
    // before anything is written.
    case_dir const dir{"kth_dirprobe_createfail"};
    registry_restore const logging;
    auto const& base = dir.path();
    auto const datadir = base / "chain";
    REQUIRE_FALSE(fs::exists(datadir));

    auto cfg = config_for(base, datadir);
    cfg.database.db_max_size = 1ULL << 60;
    node::executor host(cfg, false);

    captured_log const log;
    auto const ec = host.init_directory_if_necessary();

    // The first stage ran — the directory is there — so what failed is the
    // database, which is the branch this case is about.
    CHECK(fs::is_directory(datadir));

    CHECK(ec);
    CHECK(log.errors() >= 1);
    auto const text = log.text();
    CHECK(text.find("'Success'") == std::string::npos);
    CHECK(text.find(ec.message()) != std::string::npos);
}
#endif // ! defined(KTH_DB_READONLY)
