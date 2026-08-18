// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <vector>

#include <kth/database/block_store.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/databases/utxoz_database.hpp>
#include <kth/database/native_file.hpp>

using namespace kth;
using namespace kth::database;

// =============================================================================
// Recovering undo records at startup
// =============================================================================
//
// Undo positions live only in the header index, which is rebuilt from disk at
// startup, so they have to be recoverable from the rev files alone. Nothing
// already there could do it — order cannot, because a rev file holds records
// for a subset of its blk file's blocks with no gap marking the rest, and the
// checksum cannot, because it is seeded with the parent hash: every sibling
// validates the same record. So the record carries its owner's hash.
//
// These drive block_store directly, against real files, because what is being
// pinned is how bytes on disk are read back — including bytes no correct writer
// would produce.

namespace {

hash_digest make_hash(uint8_t seed) {
    hash_digest h{};
    h[0] = seed;
    h[31] = uint8_t(0xF0 ^ seed);
    return h;
}

utxoz::raw_outpoint make_outpoint(uint8_t seed, uint32_t index) {
    auto const txid = make_hash(seed);
    return utxoz::make_outpoint(std::span<uint8_t const, 32>(txid.data(), 32), index);
}

block_undo make_undo(uint8_t seed, uint32_t height) {
    block_undo undo;
    undo.spent.push_back({make_outpoint(seed, 0), data_chunk{0x51, 0x52, 0x53}, height});
    return undo;
}

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

// A directory of its own per test, so one leaving files behind cannot make
// another pass or fail for the wrong reason.
struct store_fixture {
    std::filesystem::path dir;
    block_store store;

    explicit store_fixture(std::string const& name)
        : dir(std::filesystem::temp_directory_path() / ("kth_undo_scan_" + name))
        , store(dir, block_store::magic_t{{0xe3, 0xe1, 0xf3, 0xe8}})
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        REQUIRE(store.initialize());
        // The store starts unable to append; the walks earn it. See
        // authorise_appends: on this empty directory they read nothing.
        authorise_appends(store);
        // initialize() reads a rev file's size only when the matching blk file
        // exists — the two are discovered together, since a rev file only ever
        // holds undo for blocks in its blk file. Production always has one; a
        // test writing undo by hand has to say so.
        make_block_file();
    }

    // Give a file number its blk file, so a later initialize() picks up the
    // matching rev size — the two are only ever discovered together.
    void make_block_file(int32_t file_num = 0) const {
        std::filesystem::create_directories(dir);
        auto const path = dir / fmt::format("blk{:05d}.dat", file_num);
        if (std::filesystem::exists(path)) return;
        FILE* file = std::fopen(path.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        std::fclose(file);
    }

    ~store_fixture() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    std::filesystem::path rev_path(int32_t file_num) const {
        return dir / fmt::format("rev{:05d}.dat", file_num);
    }
};

// Every block resolves to a parent, which is what the checksum is seeded with.
// A test that wants an unknown block returns nullopt for it.
block_store::undo_parent_lookup parents_of(
    std::vector<std::pair<hash_digest, hash_digest>> const& pairs) {
    return [pairs](hash_digest const& block_hash) -> std::optional<hash_digest> {
        for (auto const& [block, parent] : pairs) {
            if (block == block_hash) return parent;
        }
        return std::nullopt;
    };
}

// Cut a file short, the way an interrupted write leaves it.
void truncate_to(std::filesystem::path const& path, uintmax_t bytes) {
    std::error_code ec;
    std::filesystem::resize_file(path, bytes, ec);
    REQUIRE_FALSE(ec);
}

// Flip a byte in place.
void corrupt_byte(std::filesystem::path const& path, uintmax_t offset) {
    FILE* file = std::fopen(path.string().c_str(), "r+b");
    REQUIRE(file != nullptr);
    REQUIRE(std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0);
    int const original = std::fgetc(file);
    REQUIRE(original != EOF);
    REQUIRE(std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0);
    REQUIRE(std::fputc(original ^ 0xFF, file) != EOF);
    std::fclose(file);
}

} // namespace

TEST_CASE("a scan recovers every record it wrote", "[undo][scan]") {
    store_fixture fixture("roundtrip");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const block_b = make_hash(2);
    auto const parent_b = block_a;

    auto const pos_a = fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());
    auto const pos_b = fixture.store.write_undo(make_undo(11, 101), 0, block_b, parent_b);
    REQUIRE_FALSE(pos_b.is_null());

    auto const result = fixture.store.scan_undo_positions(
        parents_of({{block_a, parent_a}, {block_b, parent_b}}));

    REQUIRE(result.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(result.found.size() == 2);
    CHECK(result.found[0].block_hash == block_a);
    CHECK(result.found[0].position == pos_a.pos);
    CHECK(result.found[1].block_hash == block_b);
    CHECK(result.found[1].position == pos_b.pos);
}

TEST_CASE("a record identifies its own block, not merely its parent", "[undo][scan]") {
    // The reason the hash is in the record. Two siblings share a parent, so they
    // share the checksum seed — and two siblings built from the same
    // transactions with different coinbases spend the same outputs, so their
    // undo data is byte-identical too. Nothing about the payload or its checksum
    // can tell them apart; only the stored hash can.
    store_fixture fixture("siblings");

    auto const parent = make_hash(0);
    auto const sibling_x = make_hash(1);
    auto const sibling_y = make_hash(2);

    auto const undo = make_undo(10, 100);   // identical for both, deliberately
    auto const pos = fixture.store.write_undo(undo, 0, sibling_x, parent);
    REQUIRE_FALSE(pos.is_null());

    // Both siblings are known and share the parent, so the checksum validates
    // for either. Only the recorded hash decides.
    auto const result = fixture.store.scan_undo_positions(
        parents_of({{sibling_x, parent}, {sibling_y, parent}}));
    REQUIRE(result.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(result.found.size() == 1);
    CHECK(result.found.front().block_hash == sibling_x);

    // And reading it as the other sibling is refused, which the checksum alone
    // could never do: it satisfies both.
    auto const as_x = fixture.store.read_undo(pos, sibling_x, parent);
    CHECK(as_x.has_value());

    auto const as_y = fixture.store.read_undo(pos, sibling_y, parent);
    REQUIRE_FALSE(as_y.has_value());
    // Corruption, not absence: something pointed at another block's record, and
    // reporting that as "no undo here" would dress a damaged database as an
    // ordinary missing one.
    CHECK(as_y.error() == result_code::db_corrupt);
}

TEST_CASE("a record from before the format carried a hash is named as such", "[undo][scan]") {
    // The old layout was magic | size | payload, using the network magic — the
    // same four bytes blocks use. A reader expecting the current layout would
    // take those size bytes for the start of a hash and misread everything
    // after, which is why the record has a marker of its own. Finding the
    // network magic here is a database from an older build, and saying that is
    // what lets a caller ask for a rebuild instead of guessing.
    store_fixture fixture("legacy");

    // Hand-write a legacy record: network magic, then a size.
    auto const path = fixture.rev_path(0);
    std::filesystem::create_directories(fixture.dir);
    FILE* file = std::fopen(path.string().c_str(), "wb");
    REQUIRE(file != nullptr);
    std::array<uint8_t, 4> const network_magic{{0xe3, 0xe1, 0xf3, 0xe8}};
    REQUIRE(std::fwrite(network_magic.data(), 1, 4, file) == 4);
    uint32_t const payload_size = 64;
    REQUIRE(std::fwrite(&payload_size, sizeof(payload_size), 1, file) == 1);
    std::vector<uint8_t> const filler(payload_size + 32, 0xAB);
    REQUIRE(std::fwrite(filler.data(), 1, filler.size(), file) == filler.size());
    std::fclose(file);

    fixture.make_block_file();
    REQUIRE(fixture.store.initialize());

    auto const result = fixture.store.scan_undo_positions(parents_of({}));
    CHECK(result.status == block_store::undo_scan_status::legacy_format);
    CHECK(result.found.empty());
}

TEST_CASE("a truncated record stops the scan and yields nothing", "[undo][scan]") {
    store_fixture fixture("truncated");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const block_b = make_hash(2);

    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a).is_null());
    auto const pos_b = fixture.store.write_undo(make_undo(11, 101), 0, block_b, block_a);
    REQUIRE_FALSE(pos_b.is_null());

    // Cut into the second record's payload, so its header still fits and its
    // body does not — what an interrupted write leaves behind. Measured from the
    // write position, not from the file size: rev files are preallocated, so the
    // file is far larger than what has been written into it.
    truncate_to(fixture.rev_path(0), pos_b.pos + 2);
    fixture.make_block_file();
    REQUIRE(fixture.store.initialize());

    auto const result = fixture.store.scan_undo_positions(
        parents_of({{block_a, parent_a}, {block_b, block_a}}));

    CHECK(result.status == block_store::undo_scan_status::truncated_record);

    // And nothing is handed back, including the first record, which was intact.
    // A half-restored index is worse than an empty one: it looks complete, so a
    // block with no undo cannot be told from a block whose undo was not reached.
    CHECK(result.found.empty());
}

TEST_CASE("a record whose contents were altered fails its checksum", "[undo][scan]") {
    store_fixture fixture("corrupt");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const pos = fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a);
    REQUIRE_FALSE(pos.is_null());

    // Flip a byte of the payload, past the 40-byte header.
    corrupt_byte(fixture.rev_path(0), 45);
    fixture.make_block_file();
    REQUIRE(fixture.store.initialize());

    auto const result = fixture.store.scan_undo_positions(parents_of({{block_a, parent_a}}));
    CHECK(result.status == block_store::undo_scan_status::invalid_checksum);
    CHECK(result.block_hash == block_a);
    CHECK(result.found.empty());
}

TEST_CASE("a record for a block the index no longer holds is counted, not refused",
          "[undo][scan]") {
    // Refusing looked right and is wrong: a restart rebuilds the index from the
    // active chain only, so a branch that lost a reorganization is forgotten
    // while its undo records stay in the files. Treating that as a disagreement
    // would stop any node that ever reorganized from starting again — which is
    // how this was found, by the restart-after-reorganization test failing.
    //
    // Nothing on disk separates that from a real disagreement, and the ordinary
    // case is the first, so the record is skipped and counted.
    store_fixture fixture("unattributed");

    auto const kept = make_hash(1);
    auto const parent = make_hash(0);
    auto const forgotten = make_hash(9);

    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, forgotten, make_hash(8)).is_null());
    REQUIRE_FALSE(fixture.store.write_undo(make_undo(11, 101), 0, kept, parent).is_null());

    auto const result = fixture.store.scan_undo_positions(parents_of({{kept, parent}}));

    REQUIRE(result.status == block_store::undo_scan_status::clean_eof);
    CHECK(result.unattributed == 1);

    // And the walk continued past it: the record after the forgotten one is
    // still recovered, which it would not be if an unknown block stopped the scan.
    REQUIRE(result.found.size() == 1);
    CHECK(result.found.front().block_hash == kept);
}

TEST_CASE("two records claiming the same block are refused", "[undo][scan]") {
    store_fixture fixture("duplicate");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);

    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a).is_null());
    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a).is_null());

    auto const result = fixture.store.scan_undo_positions(parents_of({{block_a, parent_a}}));
    CHECK(result.status == block_store::undo_scan_status::duplicate_record);
    CHECK(result.block_hash == block_a);
    CHECK(result.found.empty());
}

TEST_CASE("a block with no undo record simply has none", "[undo][scan]") {
    // Side branches, blocks stored but never connected, and blocks below the
    // checkpoint never receive undo. They leave no gap and no marker — which is
    // half of why order cannot attribute records, and why the scan must not
    // infer anything from how many it finds.
    store_fixture fixture("absent");

    auto const connected = make_hash(1);
    auto const parent = make_hash(0);
    auto const side_branch = make_hash(7);

    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, connected, parent).is_null());

    auto const result = fixture.store.scan_undo_positions(
        parents_of({{connected, parent}, {side_branch, parent}}));

    REQUIRE(result.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(result.found.size() == 1);
    CHECK(result.found.front().block_hash == connected);
}

TEST_CASE("a rev file that exists and will not open is a failure, not an absence",
          "[undo][scan]") {
    // Skipping it would start the node without undo it actually has. A file that
    // is not there was never written; one that is there and will not open is a
    // different thing and has to be said so.
    store_fixture fixture("unreadable");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a).is_null());

    // A directory where the file should be: it exists, and it will not open.
    // Dropping permissions would be the obvious way and is not portable —
    // Windows ignores it, and root ignores it everywhere.
    auto const path = fixture.rev_path(0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    REQUIRE_FALSE(ec);
    REQUIRE(std::filesystem::create_directory(path, ec));

    auto const result = fixture.store.scan_undo_positions(parents_of({{block_a, parent_a}}));

    std::filesystem::remove_all(path, ec);

    CHECK(result.status == block_store::undo_scan_status::io_error);
    CHECK(result.found.empty());
}

TEST_CASE("a partial header is truncation, not the end of the file", "[undo][scan]") {
    // Between one and thirty-nine bytes of a header is an interrupted write. The
    // earlier truncation case cuts a payload, which leaves a whole header behind
    // and takes a different path.
    store_fixture fixture("partial_header");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const undo_a = make_undo(10, 100);
    auto const pos_a = fixture.store.write_undo(undo_a, 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());

    // Where the first record actually ends: its payload is the serialized undo,
    // not just the value inside it, and the checksum follows. Guessing that had
    // this test cutting into the first record instead, which takes the
    // payload-does-not-fit path the truncation case already covers.
    auto const first_record_end =
        pos_a.pos + static_cast<uint32_t>(undo_a.serialized_size()) + 32;

    // Six bytes of a second header, and nothing more.
    constexpr size_t partial_bytes = 6;
    {
        FILE* file = std::fopen(fixture.rev_path(0).string().c_str(), "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, static_cast<long>(first_record_end), SEEK_SET) == 0);
        std::array<uint8_t, partial_bytes> const partial{{'K', 'U', 'N', '2', 0x11, 0x22}};
        REQUIRE(std::fwrite(partial.data(), 1, partial.size(), file) == partial.size());
        std::fclose(file);
    }
    truncate_to(fixture.rev_path(0), first_record_end + partial_bytes);
    fixture.make_block_file();
    REQUIRE(fixture.store.initialize());

    auto const result = fixture.store.scan_undo_positions(parents_of({{block_a, parent_a}}));
    CHECK(result.status == block_store::undo_scan_status::truncated_record);
    CHECK(result.found.empty());
}

TEST_CASE("four zero bytes end the file only if the rest is zero too", "[undo][scan]") {
    // Reserved space a preallocated file never used begins with zeroes and stays
    // that way. Four zeroes with content after them is damage, and stopping there
    // would report a clean read while hiding every record beyond.
    store_fixture fixture("zeroes_then_content");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const pos_a = fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());

    auto const block_b = make_hash(2);
    auto const pos_b = fixture.store.write_undo(make_undo(11, 101), 0, block_b, block_a);
    REQUIRE_FALSE(pos_b.is_null());

    // Zero the second record's marker, leaving its body in place.
    {
        FILE* file = std::fopen(fixture.rev_path(0).string().c_str(), "r+b");
        REQUIRE(file != nullptr);
        auto const header_start = pos_b.pos - 40;
        REQUIRE(std::fseek(file, static_cast<long>(header_start), SEEK_SET) == 0);
        std::array<uint8_t, 4> const zeroes{};
        REQUIRE(std::fwrite(zeroes.data(), 1, zeroes.size(), file) == zeroes.size());
        std::fclose(file);
    }

    auto const result = fixture.store.scan_undo_positions(
        parents_of({{block_a, parent_a}, {block_b, block_a}}));

    CHECK(result.status == block_store::undo_scan_status::truncated_record);
    CHECK(result.found.empty());
}

TEST_CASE("records are recovered from more than one rev file", "[undo][scan]") {
    // The scan walks file numbers, and initialize() only learns a rev file's size
    // alongside its blk file, so more than one of each has to work.
    store_fixture fixture("multi_file");
    // file_info_ only learns about file 1 through initialize(), which discovers
    // it from the blk file — so that has to exist before anything is written to
    // rev00001.dat.
    fixture.make_block_file(1);
    REQUIRE(fixture.store.initialize());
    // A second initialize() revokes the authorisation the fixture earned: it
    // clears both cursors, on purpose, so a re-initialize cannot inherit an
    // answer about files it has not read yet (#668). Earn it again.
    authorise_appends(fixture.store);

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const block_b = make_hash(2);

    auto const pos_a = fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());
    auto const pos_b = fixture.store.write_undo(make_undo(11, 101), 1, block_b, block_a);
    REQUIRE_FALSE(pos_b.is_null());

    auto const result = fixture.store.scan_undo_positions(
        parents_of({{block_a, parent_a}, {block_b, block_a}}));

    REQUIRE(result.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(result.found.size() == 2);
    CHECK(result.found[0].file_number == 0);
    CHECK(result.found[0].block_hash == block_a);
    CHECK(result.found[1].file_number == 1);
    CHECK(result.found[1].block_hash == block_b);
}

TEST_CASE("an unknown marker is not a truncated record", "[undo][scan]") {
    // Three different things at the same place, and they read as three: the old
    // magic is a database from before the format, four zeroes are unused space,
    // and anything else is a marker that should not be there. Calling the last
    // one truncation would send whoever reads the log looking for an interrupted
    // write instead of for what actually happened.
    store_fixture fixture("bad_marker");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const undo_a = make_undo(10, 100);
    auto const pos_a = fixture.store.write_undo(undo_a, 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());

    // A whole record's worth of bytes after the first, so nothing is short —
    // only the marker is wrong.
    auto const second_start =
        pos_a.pos + static_cast<uint32_t>(undo_a.serialized_size()) + 32;
    {
        FILE* file = std::fopen(fixture.rev_path(0).string().c_str(), "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, static_cast<long>(second_start), SEEK_SET) == 0);
        std::array<uint8_t, 4> const nonsense{{'X', 'Y', 'Z', 'W'}};
        REQUIRE(std::fwrite(nonsense.data(), 1, nonsense.size(), file) == nonsense.size());
        std::array<uint8_t, 200> body;
        body.fill(0x7E);
        REQUIRE(std::fwrite(body.data(), 1, body.size(), file) == body.size());
        std::fclose(file);
    }
    // Re-read the file size: what was written by hand is past where the store
    // thinks it left off, and the scan honours the recorded size.
    fixture.make_block_file();
    REQUIRE(fixture.store.initialize());

    auto const result = fixture.store.scan_undo_positions(parents_of({{block_a, parent_a}}));
    CHECK(result.status == block_store::undo_scan_status::invalid_marker);
    CHECK(result.found.empty());
}

TEST_CASE("reading a legacy record says so, rather than blaming another block",
          "[undo][scan]") {
    // The marker has to be checked before anything after it is read. In the old
    // layout the next thirty-two bytes are a size and the start of a payload, not
    // a hash, so comparing them first would report a record belonging to another
    // block — the wrong diagnosis, under an error code that means corruption.
    store_fixture fixture("legacy_read");

    auto const path = fixture.rev_path(0);
    std::filesystem::create_directories(fixture.dir);
    {
        FILE* file = std::fopen(path.string().c_str(), "wb");
        REQUIRE(file != nullptr);
        std::array<uint8_t, 4> const network_magic{{0xe3, 0xe1, 0xf3, 0xe8}};
        REQUIRE(std::fwrite(network_magic.data(), 1, 4, file) == 4);
        uint32_t const payload_size = 8;
        REQUIRE(std::fwrite(&payload_size, sizeof(payload_size), 1, file) == 1);
        std::vector<uint8_t> const rest(payload_size + 32, 0xAB);
        REQUIRE(std::fwrite(rest.data(), 1, rest.size(), file) == rest.size());
        std::fclose(file);
    }
    fixture.make_block_file();
    REQUIRE(fixture.store.initialize());

    // The position a legacy header would have handed back: just past its 8-byte
    // header, which is where read_undo seeks backwards from.
    auto const result = fixture.store.read_undo(flat_file_pos{0, 40}, make_hash(1), make_hash(0));

    REQUIRE_FALSE(result.has_value());
    // Not db_corrupt: the record does not belong to another block, it belongs to
    // another format.
    CHECK(result.error() == result_code::other);
}

TEST_CASE("an implausible payload size is refused before anything is reserved",
          "[undo][scan]") {
    // The scanner bounds the size on the way in, but a record can be damaged
    // after that. Without the same bound here the value goes straight to an
    // allocation — gigabytes of it — and the addition of the checksum length
    // overflows first.
    store_fixture fixture("huge_size");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const pos_a = fixture.store.write_undo(make_undo(10, 100), 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());

    // The size field sits four bytes past the marker and hash.
    {
        FILE* file = std::fopen(fixture.rev_path(0).string().c_str(), "r+b");
        REQUIRE(file != nullptr);
        REQUIRE(std::fseek(file, static_cast<long>(pos_a.pos) - 4, SEEK_SET) == 0);
        uint32_t const absurd = 0xFFFFFFFFu;
        REQUIRE(std::fwrite(&absurd, sizeof(absurd), 1, file) == 1);
        std::fclose(file);
    }

    auto const result = fixture.store.read_undo(pos_a, block_a, parent_a);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == result_code::db_corrupt);
}

TEST_CASE("two records claiming one forgotten block are still refused", "[undo][scan]") {
    // Uniqueness does not depend on knowing the block. Checking it only after
    // the owner is resolved would let a duplicate pair from a branch the index
    // has forgotten pass as two ordinary unattributed records — the promise that
    // duplicates are refused holding only while the block is still known.
    store_fixture fixture("duplicate_forgotten");

    auto const forgotten = make_hash(9);
    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, forgotten, make_hash(8)).is_null());
    REQUIRE_FALSE(fixture.store.write_undo(make_undo(10, 100), 0, forgotten, make_hash(8)).is_null());

    // Nothing resolves, so both records are unattributable.
    auto const result = fixture.store.scan_undo_positions(parents_of({}));

    CHECK(result.status == block_store::undo_scan_status::duplicate_record);
    CHECK(result.block_hash == forgotten);
    CHECK(result.found.empty());
}

TEST_CASE("a path the platform can hold is a path the store can open", "[undo][scan]") {
    // std::fopen takes char const*, but a path is wchar_t on Windows. Narrowing
    // one with .string() compiles and then converts through the active code
    // page, so a data directory holding a character that page cannot represent
    // simply stops opening — and a user's own name is enough to put one there.
    // open_native hands each platform the call that takes its own path type, so
    // what the filesystem accepts is what the store can read back.
    auto const path = std::filesystem::temp_directory_path() / "kth_náïve_ñ_日本.dat";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    {
        FILE* file = open_native(path, "wb");
        REQUIRE(file != nullptr);
        uint8_t const payload[] = {0x4B, 0x54, 0x48};
        REQUIRE(std::fwrite(payload, 1, sizeof(payload), file) == sizeof(payload));
        std::fclose(file);
    }

    FILE* file = open_native(path, "rb");
    REQUIRE(file != nullptr);
    std::array<uint8_t, 3> read_back{};
    CHECK(std::fread(read_back.data(), 1, read_back.size(), file) == read_back.size());
    std::fclose(file);
    CHECK(read_back == std::array<uint8_t, 3>{{0x4B, 0x54, 0x48}});

    std::filesystem::remove(path, ec);
}

TEST_CASE("a block that spent nothing still has an undo record", "[undo][scan]") {
    // A block with nothing to restore does not produce an empty payload: the
    // count is written whatever it is, so the record is a single zero byte. A
    // payload of no bytes at all is something no writer can produce, which is
    // why both readers refuse one — the block that spends nothing is an
    // ordinary block, and it round-trips.
    store_fixture fixture("empty_undo");

    auto const block_a = make_hash(1);
    auto const parent_a = make_hash(0);
    auto const pos_a = fixture.store.write_undo(block_undo{}, 0, block_a, parent_a);
    REQUIRE_FALSE(pos_a.is_null());

    auto const record = fixture.store.read_undo(pos_a, block_a, parent_a);
    REQUIRE(record.has_value());
    CHECK(record->spent.empty());

    auto const result = fixture.store.scan_undo_positions(parents_of({{block_a, parent_a}}));
    CHECK(result.status == block_store::undo_scan_status::clean_eof);
    REQUIRE(result.found.size() == 1);
    CHECK(result.found[0].block_hash == block_a);
}
