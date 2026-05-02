// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Smoke test for the public C++ surface used by the README's
// "Using the C++ library" snippet (configuration → full_node →
// start_chain → chain().fetch_last_height → close).
//
// We don't run the snippet verbatim — the README example uses
// `network::mainnet`, whose default LMDB cap is 400 GiB and whose
// data dir is the user's chosen path. Both are wrong for CI:
//
//   * `regtest` instead of `mainnet` — same DB cap defaults in the
//     current tree, but we shrink `db_max_size` to 16 MiB explicitly
//     so the sparse map file stays bounded on every filesystem.
//   * `cfg.database.directory` is a fresh per-run temp dir, removed
//     on exit.
//   * `cfg.network.threads = 0` keeps the test offline.
//
// The point isn't to copy-paste the README at byte level — it's to
// exercise every public symbol the README example touches:
//   - `kth::node::configuration{network}`
//   - `cfg.database.directory`, `cfg.database.db_max_size`,
//     `cfg.network.threads`
//   - `kth::node::executor::do_initchain` (initialises the DB so
//     `start_chain` has something to open)
//   - `kth::node::full_node{cfg}`
//   - `node.start_chain(handler)`
//   - `node.chain().fetch_last_height(handler)`
//   - `node.close()`
//
// If any of those drifts, this binary stops compiling or returns
// non-zero, and CI flags the README as out of date.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <latch>
#include <print>
#include <random>
#include <string>
#include <system_error>
#include <thread>

#include <kth/node.hpp>

namespace fs = std::filesystem;

namespace {

struct chain_dirs {
    fs::path parent;
    fs::path chain;
};

// Returns paths whose `chain` leaf does NOT exist yet.
// `executor::do_initchain` requires that it be the one to create
// the leaf directory — if the path is already present
// `init_directory` fails with `directory_exists` and
// `do_initchain` returns false. So we claim a unique parent dir
// (retrying on collision so a stale artifact from a crashed prior
// run doesn't make us silently share it) and hand the unbuilt
// `chain` subdir to `executor`.
//
// `fs::create_directories` returns `true` only when it actually
// created the directory; if the path already exists it returns
// `false` (and `ec` stays clear), which is the collision case we
// retry past with a fresh random suffix.
//
// Failure modes (filesystem error, exhausted retries) are
// reported via `std::expected` instead of an exception so the
// test top-level can pattern-match with the rest of the
// `error_code` handling in the snippet.
std::expected<chain_dirs, std::string> make_chain_dirs() {
    auto base = fs::temp_directory_path();
    std::random_device rd;
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto name = std::string{"kth_readme_cpp_runtime_"} + std::to_string(rd());
        auto candidate = base / name;
        std::error_code ec;
        if (fs::create_directories(candidate, ec)) {
            return chain_dirs{candidate, candidate / "chain"};
        }
        if (ec) {
            return std::unexpected{
                "fs::create_directories failed: " + ec.message()};
        }
        // Path exists already (no error, just `false` return); retry.
    }
    return std::unexpected{
        std::string{"could not allocate a unique temp directory after 100 attempts"}};
}

} // namespace

int main() {
    auto dirs = make_chain_dirs();
    if ( ! dirs) {
        std::println(stderr, "make_chain_dirs failed: {}", dirs.error());
        return 1;
    }
    auto cleanup = [&]() noexcept {
        std::error_code ec;
        fs::remove_all(dirs->parent, ec);
    };

    // Same shape as the README example, but with the CI-safe overrides
    // described in the file header.
    kth::node::configuration cfg{kth::domain::config::network::regtest};
    cfg.database.directory = dirs->chain;
    cfg.database.db_max_size = 16ULL << 20; // 16 MiB
    cfg.network.threads = 0;

    // Step 1: create the chain database. The README snippet assumes the
    // user has already done this (or that the dir is populated); CI starts
    // from scratch every run so we materialise the DB here.
    {
        kth::node::executor exec{cfg, /*stdout_enabled=*/false};
        if ( ! exec.do_initchain("")) {
            std::println(stderr, "do_initchain failed");
            cleanup();
            return 1;
        }
        // exec is destroyed here, releasing the DB for the next phase.
    }

    // Step 2: the actual snippet flow on a now-initialised DB.
    bool fetch_ok = false;
    std::size_t fetched_height = 0;
    bool close_ok = false;
    {
        kth::node::full_node node{cfg};

        std::latch done{1};
        node.start_chain([&](std::error_code const& ec) {
            if (ec) {
                std::println(stderr, "start_chain failed: {}", ec.message());
                done.count_down();
                return;
            }
            node.chain().fetch_last_height(
                [&](std::error_code const& fec, std::size_t height) {
                    if ( ! fec) {
                        fetched_height = height;
                        fetch_ok = true;
                        std::println("Current height: {}", height);
                    } else {
                        std::println(stderr, "fetch_last_height failed: {}", fec.message());
                    }
                    done.count_down();
                });
        });
        done.wait();
        // `full_node::close()` reports `false` when shutdown didn't
        // complete cleanly. The README example doesn't check it
        // because it's the last thing before `main` returns and a
        // dirty exit is harmless there; in a smoke test we want a
        // shutdown regression to fail the test instead of slipping
        // through.
        close_ok = node.close();
    }

    cleanup();

    if ( ! fetch_ok) {
        std::println(stderr, "fetch_last_height did not succeed");
        return 1;
    }
    // Freshly initialised regtest = genesis only; height must be 0.
    if (fetched_height != 0) {
        std::println(stderr, "unexpected height {}", fetched_height);
        return 1;
    }
    if ( ! close_ok) {
        std::println(stderr, "node.close() reported failure");
        return 1;
    }
    return 0;
}
