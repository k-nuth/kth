// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <chrono>
#include <functional>
#include <random>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <vector>

#include <kth/database/block_store.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/database/flat_file_seq.hpp>
#include <kth/database/native_file.hpp>

using namespace kth;
using namespace kth::database;

// =============================================================================
// Where the next undo record goes after a restart
// =============================================================================
//
// A rev file is PREALLOCATED in whole chunks, so its size on disk is almost
// always larger than the bytes actually written into it. The write cursor lives
// in memory, and a restart has to put it back.
//
// What this file establishes is which of the two it puts back: the written
// extent, or the allocated size. Everything goes through the production path —
// `block_store::write_undo` to write, `initialize()` to restore,
// `scan_undo_positions` to read back — because a hole fabricated by hand would
// prove only that the reader rejects holes, which is not in question.
//
// Found while opening a real 213 GB database that had been through one restart;
// its rev files held zero gaps ending on exact 1 MiB boundaries, and 1 MiB is
// UNDOFILE_CHUNK_SIZE.

namespace {

hash_digest make_hash(uint8_t seed, uint8_t tag = 0) {
    hash_digest h{};
    h[0] = seed;
    h[1] = tag;
    h[31] = uint8_t(0xF0 ^ seed);
    return h;
}

utxoz::raw_outpoint make_outpoint(uint8_t seed, uint32_t index) {
    auto const txid = make_hash(seed);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>(txid.data(), 32), index);
}

// An undo record of a chosen payload size, so a test can land the cursor
// wherever it needs inside a chunk rather than hoping.
block_undo make_undo(uint8_t seed, uint32_t height, size_t script_bytes) {
    block_undo undo;
    undo.spent.push_back({make_outpoint(seed, 0),
                          data_chunk(script_bytes, uint8_t(0x51)), height});
    return undo;
}

// What one file looks like on disk, against an extent the CASE computed.
//
// The extent is an argument, never an inference. Deriving it here as "the last
// non-zero byte plus one" made every assertion depend on the last byte of the
// last record being non-zero — and an undo record ends in a SHA-256 checksum,
// whose final byte is zero one time in 256. The format allows it; a test must
// not require it.
//
// Passing the extent in also makes the helper check the case's arithmetic rather
// than agree with it:
//
//   * too short, and non-zero bytes turn up in what should be reserved space;
//   * too long, and the reserved space is counted as an interior hole.
//
// So a wrong expectation fails, in one direction or the other, instead of being
// quietly adopted.
struct rev_facts {
    uintmax_t physical_size{0};
    size_t interior_gaps{0};       ///< Runs of >= 64 zero bytes inside [0, extent).
    uintmax_t first_gap_start{0};
    uintmax_t first_gap_end{0};
};

// Read in fixed blocks. A blk file is 128 MiB, and slurping one into a vector per
// call — several calls per test — is a lot of memory for a question answered by
// two counters and a running position.
rev_facts measure(std::filesystem::path const& path, uintmax_t expected_extent) {
    rev_facts facts;
    std::error_code ec;
    REQUIRE(std::filesystem::exists(path, ec));
    REQUIRE_FALSE(ec);
    facts.physical_size = std::filesystem::file_size(path, ec);
    REQUIRE_FALSE(ec);

    INFO("measuring " << path.string() << " against an expected extent of " << expected_extent
         << " in a file of " << facts.physical_size);
    REQUIRE(expected_extent <= facts.physical_size);

    FILE* file = kth::database::open_native(path, "rb");
    REQUIRE(file != nullptr);

    std::array<uint8_t, 64 * 1024> buffer{};
    uintmax_t position = 0;
    uintmax_t run_start = 0;
    bool in_zeroes = false;
    bool padding_clean = true;

    while (position < facts.physical_size) {
        auto const want = static_cast<size_t>(
            std::min<uintmax_t>(buffer.size(), facts.physical_size - position));
        auto const got = std::fread(buffer.data(), 1, want, file);
        if (got != want) {
            std::fclose(file);
            FAIL("short read while measuring " << path.string());
        }

        for (size_t i = 0; i < got; ++i, ++position) {
            if (position >= expected_extent) {
                // Reserved space. Anything here means the extent was short.
                if (buffer[i] != 0) {
                    padding_clean = false;
                }
                continue;
            }

            if (buffer[i] == 0) {
                if ( ! in_zeroes) {
                    in_zeroes = true;
                    run_start = position;
                }
                continue;
            }

            if (in_zeroes && position - run_start >= 64) {
                if (facts.interior_gaps == 0) {
                    facts.first_gap_start = run_start;
                    facts.first_gap_end = position;
                }
                ++facts.interior_gaps;
            }
            in_zeroes = false;
        }
    }

    // A run of zeroes that reaches the end of the written extent is an interior
    // hole too: the reserved space begins after it, so it is not padding.
    if (in_zeroes && expected_extent - run_start >= 64) {
        if (facts.interior_gaps == 0) {
            facts.first_gap_start = run_start;
            facts.first_gap_end = expected_extent;
        }
        ++facts.interior_gaps;
    }

    REQUIRE(std::ferror(file) == 0);
    std::fclose(file);

    // Reported separately from the gaps, and as an assertion rather than a
    // returned flag: a case whose extent is wrong should say so here, not fail
    // later on a number that looks unrelated.
    CHECK(padding_clean);
    return facts;
}

// A temporary directory this run OWNS, and knows it owns.
//
// The name carries a random salt, but the guarantee does not come from the salt:
// `random_device` is allowed to be deterministic — it famously is on some
// toolchains — and a run that aborted leaves its directory behind for the next
// one to collide with. The guarantee comes from `create_directory`, which
// answers whether it CREATED the directory or found it already there. Only the
// first is ownership.
//
// On finding one taken it tries another name rather than reusing it: reusing
// would mean deleting someone else's files at the end.
class claimed_dir {
public:
    /// @param candidate Builds the n-th name to try. Injectable so a test can
    ///                  force a collision on the first one.
    explicit claimed_dir(std::function<std::string(int)> const& candidate,
                         int attempts = 16) {
        for (int attempt = 0; attempt < attempts; ++attempt) {
            auto const path = std::filesystem::temp_directory_path() / candidate(attempt);

            std::error_code ec;
            auto const created = std::filesystem::create_directory(path, ec);
            if (ec) {
                failure_ = ec;
                return;
            }
            if (created) {
                path_ = path;
                owned_ = true;
                return;
            }
            // Already there: someone else's, or a leftover. Either way, not ours.
        }
    }

    claimed_dir(claimed_dir const&) = delete;
    claimed_dir& operator=(claimed_dir const&) = delete;

    ~claimed_dir() {
        if ( ! owned_) {
            return;   // nothing was claimed, so nothing is removed
        }
        std::error_code ec;
        std::filesystem::permissions(path_, std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace, ec);
        ec.clear();
        std::filesystem::remove_all(path_, ec);
    }

    [[nodiscard]] bool owned() const { return owned_; }
    [[nodiscard]] std::error_code failure() const { return failure_; }
    [[nodiscard]] std::filesystem::path const& path() const { return path_; }

private:
    std::filesystem::path path_;
    bool owned_{false};
    std::error_code failure_;
};

// The ordinary name generator: a stem, an attempt number, and a salt that only
// has to make a collision unlikely — never to make it impossible.
// One salt for the whole run, so every temporary name in this file belongs to
// this process and to no other.
uint64_t run_salt() {
    static auto const salt = [] {
        std::random_device rd;
        return (uint64_t(rd()) << 32) ^ uint64_t(rd());
    }();
    return salt;
}

std::function<std::string(int)> salted_name(std::string const& stem) {
    return [stem](int attempt) {
        return fmt::format("kth_{}_{:016x}_{}", stem, run_salt(), attempt);
    };
}

// A directory of its own, removed on every exit.
struct scratch {
    // CLAIMED, not merely named. A constant path under the temp directory that is
    // then `remove_all`-ed is the hazard `claimed_dir` exists for, and this
    // fixture had it: two runs of this binary — two shards, or a developer
    // running the suite while CI does — would share one directory and delete each
    // other's files. The case's own label stays in the name so a failure is still
    // readable.
    claimed_dir home;
    std::filesystem::path dir;

    explicit scratch(std::string const& name)
        : home(salted_name("undo_cursor_" + name))
        , dir(home.path()) {
        REQUIRE(home.owned());
    }

    ~scratch() {
        // A case that made a file unreadable and then failed would leave it at
        // 0000, and `claimed_dir` could not remove the directory it owns. Restore
        // what is inside; the directory itself goes with `home`.
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(dir, ec);
             ! ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            std::error_code perm_ec;
            std::filesystem::permissions(it->path(), std::filesystem::perms::owner_all,
                std::filesystem::perm_options::replace, perm_ec);
        }
    }

    std::filesystem::path rev(int32_t n) const {
        return dir / fmt::format("rev{:05d}.dat", n);
    }

    // A rev file is only discovered when its blk file is: production always has
    // one, and initialize() reads the pair together.
    void make_block_file(int32_t file_num = 0) const {
        auto const path = dir / fmt::format("blk{:05d}.dat", file_num);
        if (std::filesystem::exists(path)) return;
        FILE* file = kth::database::open_native(path, "wb");
        REQUIRE(file != nullptr);
        std::fclose(file);
    }
};

// The protocol production follows, in one place: a store may not append until
// both walks have completed cleanly (#668). On a fresh directory they visit
// nothing and are trivially clean, which is exactly what a fresh production
// database does at its first start.
inline void authorise_appends(kth::database::block_store& store) {
    auto const blocks = store.scan_block_positions(
        [](int32_t, uint32_t, kth::hash_digest const&) {});
    REQUIRE(blocks.clean());
    auto const undo = store.scan_undo_positions(
        [](kth::hash_digest const&) -> std::optional<kth::hash_digest> { return std::nullopt; });
    REQUIRE(undo.status == kth::database::block_store::undo_scan_status::clean_eof);
    REQUIRE(store.append_enabled());
}

constexpr block_store::magic_t regtest_magic{{0xda, 0xb5, 0xbf, 0xfa}};

block_store::undo_parent_lookup parents_of(
    std::vector<std::pair<hash_digest, hash_digest>> const& pairs) {
    return [pairs](hash_digest const& block_hash) -> std::optional<hash_digest> {
        for (auto const& [block, parent] : pairs) {
            if (block == block_hash) return parent;
        }
        return std::nullopt;
    };
}


} // namespace

// =============================================================================
// The instrument itself
// =============================================================================
//
// Every gap assertion in this file is `interior_gaps == 0`, which is exactly the
// answer a broken `measure` would give for anything. So the detecting half is
// checked here against files whose shape is known by construction — the one
// place in this file where bytes are laid out by hand, because what is under
// test is the measuring helper and not the store.

TEST_CASE("claimed_dir - a taken candidate is passed over, not reused",
          "[database][undo][cursor]") {
    // The property the salt cannot give: if the first name is already there —
    // another run, or a leftover from one that aborted — this run must take a
    // different one and leave the first alone.
    auto const base = std::filesystem::temp_directory_path();
    auto const name = fmt::format("kth_claim_probe_taken_{:016x}", run_salt());
    auto const taken = base / name;
    auto const witness = taken / "not_mine.txt";

    std::error_code ec;
    std::filesystem::remove_all(taken, ec);
    REQUIRE(std::filesystem::create_directory(taken, ec));
    {
        FILE* file = kth::database::open_native(witness, "wb");
        REQUIRE(file != nullptr);
        REQUIRE(std::fputc('x', file) != EOF);
        std::fclose(file);
    }

    {
        // First candidate is the one already taken; the second is free.
        claimed_dir const home([&name](int attempt) {
            return attempt == 0 ? name
                                : fmt::format("kth_claim_probe_free_{:016x}_{}",
                                              run_salt(), attempt);
        });

        REQUIRE(home.owned());
        CHECK_FALSE(home.failure());
        CHECK(home.path() != taken);            // it moved on
        CHECK(std::filesystem::exists(home.path()));
    }

    // Its own directory is gone; the one it did not claim is untouched, witness
    // and all. That is the half a "remove what I was going to use" would break.
    CHECK(std::filesystem::exists(taken));
    CHECK(std::filesystem::exists(witness));

    std::filesystem::remove_all(taken, ec);
}

TEST_CASE("claimed_dir - claiming nothing removes nothing", "[database][undo][cursor]") {
    // Every candidate taken: the object owns nothing, and its destructor must
    // therefore delete nothing — least of all the directories it lost the race
    // for.
    auto const base = std::filesystem::temp_directory_path();
    auto const stem = fmt::format("kth_claim_all_taken_{:016x}", run_salt());
    auto const first = base / (stem + "_0");
    auto const second = base / (stem + "_1");

    std::error_code ec;
    for (auto const& dir : {first, second}) {
        std::filesystem::remove_all(dir, ec);
        REQUIRE(std::filesystem::create_directory(dir, ec));
    }

    {
        claimed_dir const home([&stem](int attempt) {
            return fmt::format("{}_{}", stem, attempt % 2);
        }, 4);
        CHECK_FALSE(home.owned());
        CHECK(home.path().empty());
    }

    CHECK(std::filesystem::exists(first));
    CHECK(std::filesystem::exists(second));

    std::filesystem::remove_all(first, ec);
    std::filesystem::remove_all(second, ec);
}

TEST_CASE("measure - reports a hole, its bounds and the extent", "[database][undo][cursor]") {
    scratch s("measure_selfcheck");
    auto const path = s.dir / "probe.dat";

    // 100 bytes of data, 200 zeroes, 50 bytes of data, 300 zeroes of padding.
    constexpr size_t lead = 100;
    constexpr size_t hole = 200;
    constexpr size_t after = 50;
    constexpr size_t padding = 300;
    {
        FILE* file = kth::database::open_native(path, "wb");
        REQUIRE(file != nullptr);
        std::vector<uint8_t> const data(lead, 0xAB);
        std::vector<uint8_t> const zeroes(hole, 0x00);
        std::vector<uint8_t> const more(after, 0xCD);
        std::vector<uint8_t> const tail(padding, 0x00);
        REQUIRE(std::fwrite(data.data(), 1, data.size(), file) == data.size());
        REQUIRE(std::fwrite(zeroes.data(), 1, zeroes.size(), file) == zeroes.size());
        REQUIRE(std::fwrite(more.data(), 1, more.size(), file) == more.size());
        REQUIRE(std::fwrite(tail.data(), 1, tail.size(), file) == tail.size());
        std::fclose(file);
    }

    auto const facts = measure(path, lead + hole + after);
    CHECK(facts.physical_size == lead + hole + after + padding);
    REQUIRE(facts.interior_gaps == 1);
    CHECK(facts.first_gap_start == lead);
    CHECK(facts.first_gap_end == lead + hole);
}

TEST_CASE("measure - a written extent may end in zero bytes", "[database][undo][cursor]") {
    // THE CASE THE OLD HELPER GOT WRONG. It defined the extent as "the last
    // non-zero byte plus one", so a record whose final bytes are zero measured
    // short — and an undo record ends in a SHA-256 checksum, whose last byte is
    // zero one time in 256. The format allows it; nothing may require otherwise.
    //
    // Here the written extent ends in four zeroes on purpose, and the reserved
    // space follows. The right answer is: no interior holes, and the boundary
    // exactly where the case says it is.
    scratch s("measure_zero_tail");
    auto const path = s.dir / "probe.dat";

    constexpr size_t data = 100;
    constexpr size_t zero_tail = 4;     // legitimately part of the record
    constexpr size_t padding = 200;     // reserved space after it
    constexpr size_t extent = data + zero_tail;
    {
        FILE* file = kth::database::open_native(path, "wb");
        REQUIRE(file != nullptr);
        std::vector<uint8_t> const body(data, 0xAB);
        std::vector<uint8_t> const tail(zero_tail + padding, 0x00);
        REQUIRE(std::fwrite(body.data(), 1, body.size(), file) == body.size());
        REQUIRE(std::fwrite(tail.data(), 1, tail.size(), file) == tail.size());
        std::fclose(file);
    }

    auto const facts = measure(path, extent);
    CHECK(facts.physical_size == data + zero_tail + padding);
    CHECK(facts.interior_gaps == 0);   // four zeroes are neither a hole nor padding
}

TEST_CASE("measure - a wrong extent is refused in either direction",
          "[database][undo][cursor]") {
    // The helper checks the case's arithmetic rather than agreeing with it, and
    // this is what that means. Too short and non-zero bytes turn up in what
    // should be reserved space; too long and the reserved space is counted as a
    // hole. Both are reported, so a wrong expectation cannot pass quietly.
    //
    // The failures are provoked deliberately, so they are caught here rather
    // than left to fail the run.
    scratch s("measure_wrong_extent");
    auto const path = s.dir / "probe.dat";

    constexpr size_t data = 300;
    constexpr size_t padding = 500;
    {
        FILE* file = kth::database::open_native(path, "wb");
        REQUIRE(file != nullptr);
        std::vector<uint8_t> const body(data, 0xAB);
        std::vector<uint8_t> const tail(padding, 0x00);
        REQUIRE(std::fwrite(body.data(), 1, body.size(), file) == body.size());
        REQUIRE(std::fwrite(tail.data(), 1, tail.size(), file) == tail.size());
        std::fclose(file);
    }

    // Right: nothing to report.
    CHECK(measure(path, data).interior_gaps == 0);

    // Too long: the reserved space becomes an interior hole, because something
    // is claimed to have been written after it.
    CHECK(measure(path, data + padding).interior_gaps == 1);
}

TEST_CASE("measure - counts every hole, and not the padding", "[database][undo][cursor]") {
    scratch s("measure_multi");
    auto const path = s.dir / "probe.dat";

    // Three data runs separated by two holes, then padding. And one run of
    // zeroes SHORTER than the 64-byte floor, which must not be counted.
    {
        FILE* file = kth::database::open_native(path, "wb");
        REQUIRE(file != nullptr);
        auto const put = [file](size_t n, uint8_t byte) {
            std::vector<uint8_t> const buf(n, byte);
            REQUIRE(std::fwrite(buf.data(), 1, buf.size(), file) == buf.size());
        };
        put(10, 0xAB); put(100, 0x00);     // hole 1
        put(10, 0xCD); put(8, 0x00);       // too short to count
        put(10, 0xEF); put(70, 0x00);      // hole 2
        put(10, 0x11); put(500, 0x00);     // padding
        std::fclose(file);
    }

    // Written by hand above, so the extent is the sum of what was written — the
    // 500 trailing zeroes are the reserved space and are not part of it.
    auto const facts = measure(path, 10 + 100 + 10 + 8 + 10 + 70 + 10);
    CHECK(facts.interior_gaps == 2);
    CHECK(facts.first_gap_start == 10);
    CHECK(facts.first_gap_end == 110);
}

TEST_CASE("measure - a hole that spans a read block is still one hole",
          "[database][undo][cursor]") {
    // The rewrite reads in 64 KiB blocks, so a run crossing a block boundary is
    // the case a chunked implementation gets wrong: it would report two.
    scratch s("measure_spanning");
    auto const path = s.dir / "probe.dat";

    constexpr size_t block = 64 * 1024;
    constexpr size_t lead = block - 100;    // the hole starts just before the edge
    constexpr size_t hole = 500;            // and ends well past it
    {
        FILE* file = kth::database::open_native(path, "wb");
        REQUIRE(file != nullptr);
        std::vector<uint8_t> const data(lead, 0xAB);
        std::vector<uint8_t> const zeroes(hole, 0x00);
        std::vector<uint8_t> const more(10, 0xCD);
        REQUIRE(std::fwrite(data.data(), 1, data.size(), file) == data.size());
        REQUIRE(std::fwrite(zeroes.data(), 1, zeroes.size(), file) == zeroes.size());
        REQUIRE(std::fwrite(more.data(), 1, more.size(), file) == more.size());
        std::fclose(file);
    }

    auto const facts = measure(path, lead + hole + 10);
    REQUIRE(facts.interior_gaps == 1);
    CHECK(facts.first_gap_start == lead);
    CHECK(facts.first_gap_end == lead + hole);
}

TEST_CASE("undo cursor - a restart resumes from the written extent",
          "[database][undo][cursor]") {
    // The whole defect in one sequence: write, stop, reopen, write, and look at
    // what lies between the two records.
    scratch s("restart_gap");
    s.make_block_file(0);

    auto const block_a = make_hash(0xA1);
    auto const parent_a = make_hash(0xA0);
    auto const block_b = make_hash(0xB1);
    auto const parent_b = make_hash(0xB0);

    uintmax_t written_after_session_1 = 0;
    uintmax_t restored_cursor = 0;
    uintmax_t second_record_offset = 0;

    // --- session 1: write one record and stop cleanly -----------------------
    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);

        auto const pos = store.write_undo(make_undo(0xA1, 1, 1000), 0, block_a, parent_a);
        REQUIRE(pos.file == 0);
        REQUIRE(store.flush_undo(std::array<int32_t, 1>{0}).has_value());

        written_after_session_1 = store.file_info(0).undo_size;
    }

    // Derived from the format: header (40) + payload + checksum (32), and the
    // payload wraps the 1000 script bytes in a 44-byte envelope.
    constexpr uintmax_t one_record = 40 + (1000 + 44) + 32;

    auto const after_1 = measure(s.rev(0), one_record);
    CAPTURE(after_1.physical_size, written_after_session_1);

    // The file was preallocated to a whole chunk and only partly used. If this
    // ever stops being true the rest of the test is measuring nothing.
    REQUIRE(after_1.physical_size == UNDOFILE_CHUNK_SIZE);
    REQUIRE(one_record < after_1.physical_size);
    REQUIRE(written_after_session_1 == one_record);
    REQUIRE(after_1.interior_gaps == 0);

    // --- session 2: reopen, write one more record, stop cleanly -------------
    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);

        // THE RESTORED CURSOR. This is the measurement the whole file exists for.
        restored_cursor = store.file_info(0).undo_size;

        auto const pos = store.write_undo(make_undo(0xB1, 2, 1000), 0, block_b, parent_b);
        REQUIRE(pos.file == 0);
        // write_undo returns the position of the PAYLOAD; the record starts at
        // the header before it.
        second_record_offset = pos.pos - 40;
        REQUIRE(store.flush_undo(std::array<int32_t, 1>{0}).has_value());
    }

    auto const after_2 = measure(s.rev(0), 2 * one_record);

    INFO("written extent after session 1 : " << one_record);
    INFO("physical size after session 1  : " << after_1.physical_size);
    INFO("cursor restored by session 2   : " << restored_cursor);
    INFO("offset chosen for record 2     : " << second_record_offset);
    INFO("UNDOFILE_CHUNK_SIZE            : " << UNDOFILE_CHUNK_SIZE);
    INFO("interior gaps after session 2  : " << after_2.interior_gaps);
    INFO("gap                            : " << after_2.first_gap_start
                                             << ".." << after_2.first_gap_end);

    // The cursor comes back as what was WRITTEN, not as the preallocated chunk
    // the file happens to occupy. Those two differ here by almost a megabyte,
    // which is the whole margin the defect used to fall into.
    CHECK(restored_cursor == one_record);
    CHECK(restored_cursor != after_1.physical_size);
    CHECK(second_record_offset == one_record);

    // Back to back, and nothing in between. The extent of two records was
    // asserted by the measurement itself: had it been anything else, the
    // reserved space would have held data or the padding would have counted.
    CHECK(after_2.interior_gaps == 0);

    // --- session 3: the next start reads it back ----------------------------
    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);

        auto const scan = store.scan_undo_positions(
            parents_of({{block_a, parent_a}, {block_b, parent_b}}));

        // And it opens: both records are there, in order, with nothing between
        // them for the reader to refuse.
        CHECK(scan.status == block_store::undo_scan_status::clean_eof);
        CHECK(scan.found.size() == 2);
    }
}

TEST_CASE("undo cursor - one session writes no hole at all", "[database][undo][cursor]") {
    // The control for the control. Two records in ONE session sit back to back,
    // and the scan reads them: so the hole above is created by the restart and
    // not by writing two records.
    scratch s("single_session");
    s.make_block_file(0);

    auto const block_a = make_hash(0xA1);
    auto const parent_a = make_hash(0xA0);
    auto const block_b = make_hash(0xB1);
    auto const parent_b = make_hash(0xB0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);

        auto const first = store.write_undo(make_undo(0xA1, 1, 1000), 0, block_a, parent_a);
        auto const second = store.write_undo(make_undo(0xB1, 2, 1000), 0, block_b, parent_b);
        REQUIRE(first.file == 0);
        REQUIRE(second.file == 0);

        // Back to back: the second record's header begins where the first
        // record's checksum ended.
        CHECK(second.pos - 40 == first.pos - 40 + 40 + 1000 + 44 + 32);
        REQUIRE(store.flush_undo(std::array<int32_t, 1>{0}).has_value());
    }

    // Two records back to back: header (40) + payload (1000 + 44) + checksum (32)
    // each, derived from the format rather than read off the file.
    auto const facts = measure(s.rev(0), 2 * (40 + (1000 + 44) + 32));
    CHECK(facts.interior_gaps == 0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        auto const scan = store.scan_undo_positions(
            parents_of({{block_a, parent_a}, {block_b, parent_b}}));
        CHECK(scan.status == block_store::undo_scan_status::clean_eof);
        CHECK(scan.found.size() == 2);
    }
}

TEST_CASE("undo cursor - no number of restarts adds a hole", "[database][undo][cursor]") {
    // The defect accumulated one hole per reopen, which is why a database
    // restarted twice was refused at the first one and never reached the rest.
    // Four sessions here, and the records simply follow one another.
    scratch s("repeated");
    s.make_block_file(0);

    std::vector<std::pair<hash_digest, hash_digest>> known;
    std::vector<uintmax_t> offsets;

    for (uint8_t session = 0; session < 4; ++session) {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);

        auto const block = make_hash(uint8_t(0xC0 + session));
        auto const parent = make_hash(uint8_t(0xB0 + session));
        known.emplace_back(block, parent);

        auto const pos = store.write_undo(make_undo(uint8_t(0xC0 + session),
                                                    uint32_t(session + 1), 1000),
                                          0, block, parent);
        REQUIRE(pos.file == 0);
        offsets.push_back(pos.pos - 40);
        REQUIRE(store.flush_undo(std::array<int32_t, 1>{0}).has_value());
    }

    CAPTURE(offsets);

    // Derived from the format rather than from the store: every record here has
    // the same 1000 script bytes, so they are evenly spaced by construction.
    constexpr uintmax_t record = 40 + (1000 + 44) + 32;

    REQUIRE(offsets.size() == 4);
    for (size_t i = 0; i < offsets.size(); ++i) {
        CAPTURE(i, offsets[i]);
        CHECK(offsets[i] == uintmax_t(i) * record);
    }

    auto const facts = measure(s.rev(0), 4 * record);
    CHECK(facts.interior_gaps == 0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        auto const scan = store.scan_undo_positions(parents_of(known));
        CHECK(scan.status == block_store::undo_scan_status::clean_eof);
        CHECK(scan.found.size() == 4);   // every one of them, across four sessions
    }
}

TEST_CASE("undo cursor - a record that fills its chunk exactly leaves no hole",
          "[database][undo][cursor]") {
    // The boundary case, and the reason this is easy to miss in a short test: when
    // the written extent happens to land exactly on a chunk boundary, the
    // allocated size and the written extent agree and the restart resumes in the
    // right place. A database is only damaged when the last chunk is partly used —
    // which, for any real payload size, is almost always.
    scratch s("exact_fit");
    s.make_block_file(0);

    // Header (40) + payload + checksum (32) == one chunk exactly. The payload's
    // envelope around the script bytes is MEASURED rather than assumed: the
    // script length is a varint, so it is two bytes wider at a megabyte than it
    // is at a kilobyte, and a hand-counted constant lands just past the boundary
    // and quietly stops testing this case.
    auto const script_bytes = [] {
        size_t guess = UNDOFILE_CHUNK_SIZE - (40 + 44 + 32);
        for (int i = 0; i < 8; ++i) {
            auto const total = 40 + make_undo(0xA1, 1, guess).serialized_size() + 32;
            if (total == UNDOFILE_CHUNK_SIZE) break;
            // Both directions, and neither subtraction may wrap: these are
            // size_t, so an overshoot computed the other way round would produce
            // an enormous guess and the loop would never converge.
            if (total > UNDOFILE_CHUNK_SIZE) {
                auto const over = total - UNDOFILE_CHUNK_SIZE;
                if (over >= guess) break;
                guess -= over;
            } else {
                guess += UNDOFILE_CHUNK_SIZE - total;
            }
        }
        return guess;
    }();
    REQUIRE(40 + make_undo(0xA1, 1, script_bytes).serialized_size() + 32
        == UNDOFILE_CHUNK_SIZE);

    auto const block_a = make_hash(0xA1);
    auto const parent_a = make_hash(0xA0);
    auto const block_b = make_hash(0xB1);
    auto const parent_b = make_hash(0xB0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        auto const pos = store.write_undo(make_undo(0xA1, 1, script_bytes), 0, block_a, parent_a);
        REQUIRE(pos.file == 0);
        REQUIRE(store.flush_undo(std::array<int32_t, 1>{0}).has_value());
    }

    // The record fills the chunk exactly, so the extent IS the chunk and there is
    // no reserved space at all. Passing anything else here would fail inside
    // `measure`, which is what makes this the case it says it is.
    auto const after_1 = measure(s.rev(0), UNDOFILE_CHUNK_SIZE);
    CAPTURE(after_1.physical_size);
    REQUIRE(after_1.physical_size == UNDOFILE_CHUNK_SIZE);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        CHECK(store.file_info(0).undo_size == UNDOFILE_CHUNK_SIZE);   // they agree
        auto const pos = store.write_undo(make_undo(0xB1, 2, 1000), 0, block_b, parent_b);
        REQUIRE(pos.file == 0);
        REQUIRE(store.flush_undo(std::array<int32_t, 1>{0}).has_value());
    }

    CHECK(measure(s.rev(0), UNDOFILE_CHUNK_SIZE + (40 + (1000 + 44) + 32)).interior_gaps == 0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        auto const scan = store.scan_undo_positions(
            parents_of({{block_a, parent_a}, {block_b, parent_b}}));
        CHECK(scan.status == block_store::undo_scan_status::clean_eof);
    }
}

// =============================================================================
// The same question for the block files
// =============================================================================
//
// `find_block_pos` takes its cursor from `file_info_[n].size`, restored the same
// way at `block_store.cpp:129`, and `block_files_` preallocates in chunks of its
// own. The structure is identical, so the answer should be too — but the real
// database that exposed this shows no gaps in its blk files, because the run that
// restarted stored no blocks. Structure is not evidence; this measures it.

TEST_CASE("block cursor - a restart continues in the same block file", "[database][undo][cursor]") {
    // The same root cause reached blocks with a different symptom, and it is
    // worth keeping both in view.
    //
    // A blk file is preallocated a whole 128 MiB chunk at its first write, and
    // the rotation test is `size + add_size >= MAX_BLOCKFILE_SIZE`. With the
    // cursor restored from the PHYSICAL size, a restart made the store believe
    // the file was already full and start a new one — no hole, but a chunk
    // abandoned per restart per open file. With the cursor restored from the
    // written extent, the next block simply follows the last one.
    scratch s("block_restart");

    data_chunk const raw(1000, uint8_t(0x77));

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        auto const pos = store.save_block_raw(raw, 1, 1000000);
        REQUIRE(pos.file == 0);
    }

    auto const blk0 = s.dir / "blk00000.dat";
    // magic (4) + size (4) + the raw block.
    constexpr uintmax_t one_block = 4 + 4 + 1000;

    auto const after_1 = measure(blk0, one_block);

    // Preallocated to a whole chunk, barely used. This is the state every clean
    // stop leaves behind.
    REQUIRE(after_1.physical_size == BLOCKFILE_CHUNK_SIZE);
    REQUIRE(one_block < after_1.physical_size);

    int32_t second_file = -1;
    uintmax_t restored_cursor = 0;
    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        restored_cursor = store.file_info(0).size;

        auto const pos = store.save_block_raw(raw, 2, 1000600);
        second_file = pos.file;
    }

    INFO("written extent after session 1 : " << one_block);
    INFO("physical size after session 1  : " << after_1.physical_size);
    INFO("cursor restored by session 2   : " << restored_cursor);
    INFO("file chosen for block 2        : " << second_file);

    // The cursor is the written extent, not the chunk the file occupies.
    CHECK(restored_cursor == one_block);
    CHECK(restored_cursor != after_1.physical_size);

    // Same file, no rotation, and the second block right after the first. The
    // two-record extent is asserted by the measurement itself.
    auto const after_2 = measure(blk0, 2 * one_block);
    CHECK(second_file == 0);
    CHECK(after_2.interior_gaps == 0);
}

// =============================================================================
// Appending is earned (#668)
// =============================================================================
//
// The cursor is no longer guessed from the file's size, so between `initialize()`
// and the two walks the store does not know where its files end. What it does
// with that is the contract: it refuses, in every build, as a value the caller
// can see — never an assertion that disappears in Release, and never a fallback
// to the size that caused this in the first place.

TEST_CASE("append contract - a fresh store refuses to place a block",
          "[database][undo][cursor]") {
    scratch s("contract_block");
    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());

    CHECK_FALSE(store.append_enabled());
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
}

TEST_CASE("append contract - a fresh store refuses to place an undo record",
          "[database][undo][cursor]") {
    scratch s("contract_undo");
    s.make_block_file(0);
    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());

    CHECK_FALSE(store.append_enabled());
    CHECK(store.write_undo(make_undo(0xA1, 1, 1000), 0, make_hash(0xA1), make_hash(0xA0))
        .is_null());
}

TEST_CASE("append contract - both walks are required, not either",
          "[database][undo][cursor]") {
    // One family adopted is not enough. The store writes into both kinds of file
    // and a cursor it has not earned is a cursor it must not use, whichever one
    // it is.
    scratch s("contract_both");
    s.make_block_file(0);
    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());

    auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    REQUIRE(blocks.clean());
    CHECK_FALSE(store.append_enabled());          // blocks alone: still refused
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());

    auto const undo = store.scan_undo_positions(
        [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
    REQUIRE(undo.status == block_store::undo_scan_status::clean_eof);
    CHECK(store.append_enabled());                 // and now both
    CHECK_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
}

TEST_CASE("append contract - a failed walk leaves the store refusing",
          "[database][undo][cursor]") {
    // The case the whole contract exists for: a database whose files cannot be
    // read end to end is not one to append to. Here a blk file carries data after
    // a run of zeroes — indistinguishable, from below, from the hole this defect
    // used to leave — and the walk refuses rather than calling it an ending.
    scratch s("contract_failed_walk");

    // Write one block through the real path, then reopen and put data past a gap
    // by hand: this is the READER being tested, so the file is allowed to be one
    // no correct writer would produce.
    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
    }

    auto const blk = s.dir / "blk00000.dat";
    constexpr uintmax_t extent = 4 + 4 + 1000;   // magic + size + the raw block
    measure(blk, extent);                 // and the file really does end there
    {
        FILE* file = kth::database::open_native(blk, "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, static_cast<long>(extent + 4096), SEEK_SET) == 0);
        std::array<uint8_t, 8> const junk{{0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4}};
        REQUIRE(std::fwrite(junk.data(), 1, junk.size(), file) == junk.size());
        std::fclose(file);
    }

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());

    auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    CHECK_FALSE(blocks.clean());
    CHECK(blocks.status == block_store::block_scan_status::bad_magic);
    CHECK(blocks.file_number == 0);
    CHECK(blocks.position == extent);

    // And the refusal holds: no cursor, so nothing can be written over the data
    // the walk stopped understanding.
    CHECK_FALSE(store.append_enabled());
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
}

TEST_CASE("append contract - re-initialising revokes it", "[database][undo][cursor]") {
    scratch s("contract_reinit");
    s.make_block_file(0);
    block_store store(s.dir, regtest_magic);

    REQUIRE(store.initialize());
    authorise_appends(store);
    REQUIRE(store.append_enabled());

    // The reopen path. Whatever the last run established was about files this one
    // has not read yet.
    REQUIRE(store.initialize());
    CHECK_FALSE(store.append_enabled());
    CHECK(store.write_undo(make_undo(0xA1, 1, 1000), 0, make_hash(0xA1), make_hash(0xA0))
        .is_null());

    authorise_appends(store);
    CHECK(store.append_enabled());
}

// =============================================================================
// A file that is not there, and a file that should be (#668)
// =============================================================================
//
// "Absent" and "empty" are the same thing only at the end of the numbering. In
// the middle they are not: a blk file missing between two that exist is a file
// that was lost, and treating it as nothing to read would let the walk sail past
// it and hand out a cursor over a chain with a piece cut out of it.

TEST_CASE("missing files - a fresh database scans clean", "[database][undo][cursor]") {
    scratch s("missing_fresh");
    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());

    auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    CHECK(blocks.clean());
    CHECK(blocks.found == 0);
}

TEST_CASE("missing files - the file after the last one is a legitimate end",
          "[database][undo][cursor]") {
    // blk00000 exists and blk00001 does not. That is where the numbering stops,
    // not a gap, and it must not be an error.
    scratch s("missing_after_end");
    s.make_block_file(0);

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());
    authorise_appends(store);
    REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());

    block_store reopened(s.dir, regtest_magic);
    REQUIRE(reopened.initialize());
    auto const blocks = reopened.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    CHECK(blocks.clean());
    CHECK(blocks.found == 1);
}

TEST_CASE("missing files - a rev file with no records is an absence, not a fault",
          "[database][undo][cursor]") {
    // A blk file whose blocks produced no undo has no rev file at all. Ordinary:
    // undo is written for the blocks that need it, not for every file number.
    scratch s("missing_rev");
    s.make_block_file(0);

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());
    auto const undo = store.scan_undo_positions(
        [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
    CHECK(undo.status == block_store::undo_scan_status::clean_eof);
    CHECK(undo.found.empty());
}

TEST_CASE("missing files - a hole in the numbering is not an ending",
          "[database][undo][cursor]") {
    // blk00000 and blk00002 exist, blk00001 does not, and the store knows the
    // numbering runs that far. The walk must not report a clean read of a chain
    // whose middle file is gone.
    scratch s("missing_interior");

    // A block in each, staged one open at a time for the reason spelled out in
    // the atomicity case: the file that goes missing has to be one that held
    // something, or this would only prove that an empty file can vanish.
    for (int32_t target = 0; target < 3; ++target) {
        s.make_block_file(target);
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        auto const pos = store.save_block_raw(data_chunk(1000, uint8_t(0x70 + target)),
                                              uint32_t(target) + 1, 1000000 + target);
        REQUIRE_FALSE(pos.is_null());
        REQUIRE(pos.file == target);
    }
    measure(s.dir / "blk00001.dat", 4 + 4 + 1000);   // it holds exactly one block

    // Now lose the middle one, with its block in it.
    std::error_code ec;
    std::filesystem::remove(s.dir / "blk00001.dat", ec);
    REQUIRE_FALSE(ec);

    // Refused before a walk even starts: the numbering itself says a file is
    // gone, and nothing downstream should have to infer it from a short read.
    block_store reopened(s.dir, regtest_magic);
    CHECK_FALSE(reopened.initialize());
    CHECK_FALSE(reopened.append_enabled());
}

// Can this process be denied a file it owns? Root cannot, and neither can some
// filesystems. Asked BEFORE the case that depends on it, so a platform that
// cannot stage the condition skips the test instead of passing it.
namespace {

bool can_be_denied_own_file() {
    claimed_dir const home(salted_name("perm_probe"));
    if ( ! home.owned()) return false;

    auto const probe = home.path() / "probe.dat";
    std::error_code ec;

    FILE* file = kth::database::open_native(probe, "wb");
    if (file == nullptr) return false;
    std::fputc('x', file);
    std::fclose(file);

    std::filesystem::permissions(probe, std::filesystem::perms::none,
        std::filesystem::perm_options::replace, ec);

    FILE* reopened = kth::database::open_native(probe, "rb");
    auto const denied = (reopened == nullptr);
    if (reopened != nullptr) std::fclose(reopened);

    std::filesystem::permissions(probe, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace, ec);
    return denied;   // the directory, and only it, goes with `home`
}

// Can this process traverse a directory it cannot list? That is what makes the
// listing failure reachable while the per-file checks still succeed.
bool can_list_be_denied_while_traversing() {
    claimed_dir const home(salted_name("listing_probe"));
    if ( ! home.owned()) return false;

    auto const& probe = home.path();
    std::error_code ec;

    FILE* file = kth::database::open_native(probe / "inside.dat", "wb");
    if (file == nullptr) return false;
    std::fputc('x', file);
    std::fclose(file);

    std::filesystem::permissions(probe, std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, ec);

    std::error_code list_ec;
    std::filesystem::directory_iterator it(probe, list_ec);
    std::error_code stat_ec;
    auto const reachable = std::filesystem::exists(probe / "inside.dat", stat_ec);

    std::filesystem::permissions(probe, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace, ec);

    // Exactly the shape the case needs: listing refused, contents reachable.
    return static_cast<bool>(list_ec) && reachable && ! stat_ec;
}

} // namespace

TEST_CASE("missing files - a file that exists and will not open is refused",
          "[database][undo][cursor]") {
    // The distinction that matters most: not there is nothing to read; there and
    // unreadable is an extent nobody knows, and an unknown extent must never
    // become a place to write.
    //
    // SKIPPED rather than passed where the condition cannot be staged — running
    // as root, or on a filesystem without permissions. A test that reports green
    // without having provoked the refusal claims a guarantee it did not check,
    // and this is the one case whose whole point is the refusal.
    if ( ! can_be_denied_own_file()) {
        SKIP("this process cannot be denied a file it owns (running as root, or a "
             "filesystem without permissions), so the refusal cannot be provoked here");
    }

    scratch s("missing_unreadable");
    s.make_block_file(0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
    }

    std::filesystem::permissions(s.dir / "blk00000.dat",
        std::filesystem::perms::none, std::filesystem::perm_options::replace);

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());
    auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});

    // Hard assertions now: the probe above established that the file really is
    // unreadable to this process.
    REQUIRE_FALSE(blocks.clean());
    CHECK(blocks.status == block_store::block_scan_status::open_failed);
    CHECK(blocks.file_number == 0);
    CHECK_FALSE(store.append_enabled());
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x99)), 2, 1000600).is_null());

    std::filesystem::permissions(s.dir / "blk00000.dat",
        std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
}

// =============================================================================
// What a second initialize() leaves behind (#668)
// =============================================================================

TEST_CASE("reinitialise - a failed initialize keeps nothing from the last session",
          "[database][undo][cursor]") {
    // A store that worked, then an initialize that refuses. What must NOT happen
    // is that it carries on with the cursors it earned before: those describe
    // files this initialize did not accept.
    scratch s("reinit_failed");
    s.make_block_file(0);

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());
    authorise_appends(store);
    REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
    REQUIRE(store.append_enabled());

    // Now make the numbering inconsistent under it and initialize again.
    s.make_block_file(2);
    CHECK_FALSE(store.initialize());

    // Refused, and the previous session's authority is gone with it.
    CHECK_FALSE(store.append_enabled());
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 2, 1000600).is_null());
    CHECK(store.write_undo(make_undo(0xA1, 1, 1000), 0, make_hash(0xA1), make_hash(0xA0))
        .is_null());
}

TEST_CASE("reinitialise - a later successful initialize adopts only new results",
          "[database][undo][cursor]") {
    scratch s("reinit_recovers");
    s.make_block_file(0);

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());
    authorise_appends(store);
    REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());

    // Second open of the same instance, over the same files.
    REQUIRE(store.initialize());
    CHECK_FALSE(store.append_enabled());

    size_t seen = 0;
    auto const blocks = store.scan_block_positions(
        [&seen](int32_t, uint32_t, hash_digest const&) { ++seen; });
    REQUIRE(blocks.clean());
    CHECK(blocks.found == 1);
    CHECK(seen == 1);

    auto const undo = store.scan_undo_positions(
        [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
    REQUIRE(undo.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(store.append_enabled());

    // And the cursor is the one this walk measured: the next block follows the
    // first, in the same file, with nothing in between.
    constexpr uintmax_t record = 4 + 4 + 1000;
    auto const pos = store.save_block_raw(data_chunk(1000, uint8_t(0x88)), 2, 1000600);
    REQUIRE_FALSE(pos.is_null());
    CHECK(pos.file == 0);
    CHECK(pos.pos == record + 8);   // magic + size, then the payload
    CHECK(measure(s.dir / "blk00000.dat", 2 * (4 + 4 + 1000)).interior_gaps == 0);
}

TEST_CASE("publication is atomic - a walk that fails near the end publishes nothing",
          "[database][undo][cursor]") {
    // Three files, all good but the last, and the failure comes after two of them
    // were already measured. Nothing may be adopted: half a set of cursors is
    // worse than none, because the files it reached would accept appends and the
    // rest would not.
    scratch s("atomic_publish");

    // One block per file, and it takes three opens to get there: `find_block_pos`
    // starts at `last_block_file_`, which initialize() sets to the highest file
    // that exists, and it only rotates off a file whose size is non-zero. Writing
    // three blocks in one session would put all three in the highest file and
    // leave the other two empty — which would make the assertion below ("the
    // files that read cleanly are not adopted") true of nothing.
    for (int32_t target = 0; target < 3; ++target) {
        s.make_block_file(target);
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        auto const pos = store.save_block_raw(data_chunk(1000, uint8_t(0x70 + target)),
                                              uint32_t(target) + 1, 1000000 + target);
        REQUIRE_FALSE(pos.is_null());
        REQUIRE(pos.file == target);
    }

    // All three really do hold a block.
    for (int32_t i = 0; i < 3; ++i) {
        CAPTURE(i);
        measure(s.dir / fmt::format("blk{:05d}.dat", i), 4 + 4 + 1000);
    }

    // Damage the LAST file only, past a run of zeroes.
    auto const blk2 = s.dir / "blk00002.dat";
    constexpr uintmax_t extent = 4 + 4 + 1000;
    measure(blk2, extent);
    {
        FILE* file = kth::database::open_native(blk2, "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, static_cast<long>(extent + 8192), SEEK_SET) == 0);
        std::array<uint8_t, 8> const junk{{0xde, 0xad, 0xbe, 0xef, 9, 9, 9, 9}};
        REQUIRE(std::fwrite(junk.data(), 1, junk.size(), file) == junk.size());
        std::fclose(file);
    }

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());

    auto const blocks = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    REQUIRE_FALSE(blocks.clean());
    CHECK(blocks.file_number == 2);

    // The two files that read cleanly hold a block each, were measured, and are
    // NOT adopted: `file_info` still reports the zero it was constructed with,
    // and no position can be issued.
    CHECK(store.file_info(0).size == 0);
    CHECK(store.file_info(1).size == 0);
    CHECK_FALSE(store.append_enabled());
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x99)), 4, 1000900).is_null());

    // And an undo walk afterwards cannot rescue it on its own: both families are
    // required, and the block family never got there.
    auto const undo = store.scan_undo_positions(
        [](hash_digest const&) -> std::optional<hash_digest> { return std::nullopt; });
    CHECK(undo.status == block_store::undo_scan_status::clean_eof);
    CHECK_FALSE(store.append_enabled());
}

TEST_CASE("publication is atomic - a failing rescan revokes an earlier adoption",
          "[database][undo][cursor]") {
    // The other direction, and the one an all-or-nothing publication does not
    // cover on its own: the store already had a clean pair, and a later walk
    // fails. Leaving the earlier authority standing would let it go on appending
    // at cursors measured before whatever just stopped being readable.
    scratch s("atomic_revoke");
    s.make_block_file(0);

    block_store store(s.dir, regtest_magic);
    REQUIRE(store.initialize());
    authorise_appends(store);
    REQUIRE(store.append_enabled());
    REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());

    // Put data past a run of zeroes, then rescan on the SAME instance.
    auto const blk = s.dir / "blk00000.dat";
    constexpr uintmax_t extent = 4 + 4 + 1000;   // magic + size + the raw block
    measure(blk, extent);                 // and the file really does end there
    {
        FILE* file = kth::database::open_native(blk, "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, static_cast<long>(extent + 4096), SEEK_SET) == 0);
        std::array<uint8_t, 8> const junk{{0xde, 0xad, 0xbe, 0xef, 5, 6, 7, 8}};
        REQUIRE(std::fwrite(junk.data(), 1, junk.size(), file) == junk.size());
        std::fclose(file);
    }

    auto const rescan = store.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {});
    REQUIRE_FALSE(rescan.clean());

    // The authority earned before it is gone.
    CHECK_FALSE(store.append_enabled());
    CHECK(store.save_block_raw(data_chunk(1000, uint8_t(0x88)), 2, 1000600).is_null());
}

TEST_CASE("missing files - a directory that cannot be listed is refused",
          "[database][undo][cursor]") {
    // The gap check reads the directory to find the highest file number. If that
    // listing cannot be read, a lost interior file would go unnoticed and both
    // walks would publish cursors for files nobody enumerated — so the listing
    // failing is itself a refusal, not a reason to skip the check.
    //
    // Skipped, not passed, where the condition cannot be staged: same reason as
    // the unreadable-file case.
    if ( ! can_list_be_denied_while_traversing()) {
        SKIP("this process cannot be denied a directory listing it can still traverse, so the "
             "listing failure cannot be staged here");
    }

    scratch s("unlistable_dir");
    s.make_block_file(0);

    {
        block_store store(s.dir, regtest_magic);
        REQUIRE(store.initialize());
        authorise_appends(store);
        REQUIRE_FALSE(store.save_block_raw(data_chunk(1000, uint8_t(0x77)), 1, 1000000).is_null());
    }

    // EXECUTE but not READ: the files inside can still be reached by name, so the
    // discovery loop gets its answers, and only the listing that the gap check
    // needs fails. Removing all permissions instead would stop the walk earlier,
    // at the first `exists`, and never reach the branch this case is about.
    std::filesystem::permissions(s.dir, std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);

    block_store store(s.dir, regtest_magic);
    auto const opened = store.initialize();

    std::filesystem::permissions(s.dir, std::filesystem::perms::owner_all,
        std::filesystem::perm_options::replace);

    CHECK_FALSE(opened);
    CHECK_FALSE(store.append_enabled());
}
