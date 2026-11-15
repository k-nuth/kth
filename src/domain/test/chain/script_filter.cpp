// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <kth/domain/chain/script_filter.hpp>

using namespace kth;
using namespace kd;

namespace {

constexpr std::string_view script_filter_leaf_hex[] = {
    "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb010000000000000000",
    "11633077d74aa1a6958c6f1162c9e935c628c6b004d167165f0a4cccd020eecb020000000000000001",
    "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e010000000100000001",
    "46402f61b3239cd80b93294e10b552d66a31b79c8fe7fdd92a832d00f832e82e020000000000000000",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24010000000000000002",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24010000000000000003",
    "5c1b924542fecd099a9568826bce5018476b3c2a29944c9c7ff6e47f80d21d24020000000000000003",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e000000000000000000",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e010000000000000001",
    "e2f6c234985456932db846b2bf1a2fed2cb98b5b0f246f30711ea6148a76860e010000000100000000",
};

struct root_case {
    size_t count;
    std::string_view root;
};

constexpr root_case script_filter_roots[] = {
    {0, "0000000000000000000000000000000000000000000000000000000000000000"},
    {1, "9d1f540224023621706bc5c9a4cb19c21abb295d62aebaa75137bbcb44dadfb5"},
    {2, "e5c9cff033bb91a3f42a868728b860c8a4eb1e6047d483b7b3a066f694f2180e"},
    {3, "bbf4f9772400cd1355cfd4c39fb37e5f03a63e2452b1ca97deba45d475771dc2"},
    {5, "378bce73f320bd2b71796e5731f6e91d71e42ecf0513d1912f0e9439fb52a2b9"},
    {10, "f66125233da123c29e93f9510c96557d5edfc071225ea7796c86e6deba8c23fd"},
};

struct range_case {
    std::string_view name;
    size_t lo;
    size_t end;
    std::array<std::string_view, 4> hashes;
    size_t hash_count;
};

constexpr range_case script_filter_ranges[] = {
    {"leaves 3 to 5", 3, 6, {"2512bd80ee8cef99a54e819ae61fdba6da828e4f874b8ecba5e47559c616c476", "e5c9cff033bb91a3f42a868728b860c8a4eb1e6047d483b7b3a066f694f2180e", "9321aa6706ebb15995fd96c21947d13ac08de5651b28424e013249562233b815", "a3906f23297e3c240c522789fe69efeb3f125de38ed0627c8a02458599b47fb2"}, 4},
    {"leaf 9", 9, 10, {"93aba49a21f3ff1d74de4f2ddfd5996c106cced8d8271325333fd507c7580a29", "bc683ecc5d6d6a2e7a734da1f2d778330e467c2ef845e2ef04dbdb86dc4b90f3", {}, {}}, 2},
    {"leaf 0", 0, 1, {"2892d10bbf543b5b6acb23ef107edeb7ed945fdd1cb928a0ce4c5390c75b5ca0", "560371e2c34b9f6c70d4c978410b91dfa757234e81a4c7553690fa75f2fe20fd", "0c661e38a5a8a7b99ecc5a4ef8edf3ed232a443151a05da5feee0993377117c5", "a3906f23297e3c240c522789fe69efeb3f125de38ed0627c8a02458599b47fb2"}, 4},
    {"leaves 3 to 7", 3, 8, {"2512bd80ee8cef99a54e819ae61fdba6da828e4f874b8ecba5e47559c616c476", "e5c9cff033bb91a3f42a868728b860c8a4eb1e6047d483b7b3a066f694f2180e", "a3906f23297e3c240c522789fe69efeb3f125de38ed0627c8a02458599b47fb2", {}}, 3},
    {"leaves 6 and 7", 6, 8, {"7fa40cb37ecdbeaaf92ea60c633b2865db56d1491be3a35a362f10b37ff451dc", "1cbf127a61c9b772d854662f32960f9bf4c05934e10b3c3b69b4075d6ed5e964", "a3906f23297e3c240c522789fe69efeb3f125de38ed0627c8a02458599b47fb2", {}}, 3},
    {"every leaf", 0, 10, {}, 0},
};

constexpr bool published_hex_widths() {
    for (auto const hex : script_filter_leaf_hex) {
        if (hex.size() != chain::script_leaf_size * 2) {
            return false;
        }
    }
    for (auto const& item : script_filter_roots) {
        if (item.root.size() != hash_size * 2) {
            return false;
        }
    }
    for (auto const& item : script_filter_ranges) {
        if (item.hash_count > item.hashes.size() || item.lo >= item.end) {
            return false;
        }
        for (size_t i = 0; i < item.hash_count; ++i) {
            if (item.hashes[i].size() != hash_size * 2) {
                return false;
            }
        }
    }
    return true;
}

static_assert(published_hex_widths());

hash_digest require_hash(std::string_view hex) {
    auto const decoded = decode_base16<hash_size>(hex);
    REQUIRE(decoded);
    return *decoded;
}

std::vector<chain::script_leaf> script_filter_leaves() {
    std::vector<chain::script_leaf> leaves;
    leaves.reserve(std::size(script_filter_leaf_hex));
    for (auto const hex : script_filter_leaf_hex) {
        auto const bytes = decode_base16(hex);
        REQUIRE(bytes);
        auto const leaf = chain::parse_script_leaf(*bytes);
        REQUIRE(leaf);
        leaves.push_back(*leaf);
    }
    return leaves;
}

// The run and the neighbour pointers refer to `leaves`, which the caller keeps alive.
// The edge hashes cover the run plus those neighbours, the span a proof has to bind.
struct script_range_answer {
    uint32_t count;
    uint32_t first;
    std::span<chain::script_leaf const> run;
    chain::script_leaf const* before;
    chain::script_leaf const* after;
    std::vector<hash_digest> hashes;
};

script_range_answer honest_script_range(std::span<chain::script_leaf const> leaves, hash_digest const& prefix, uint16_t bits) {
    auto const found = chain::script_leaves_with_prefix(leaves, prefix, bits);
    REQUIRE(found);

    script_range_answer answer{};
    answer.count = static_cast<uint32_t>(leaves.size());
    answer.first = static_cast<uint32_t>(found->first);
    answer.run = leaves.subspan(found->first, found->end - found->first);
    answer.before = nullptr;
    answer.after = nullptr;

    auto lo = found->first;
    auto hi = found->end;
    if (found->first > 0) {
        answer.before = &leaves[found->first - 1];
        --lo;
    }
    if (found->end < leaves.size()) {
        answer.after = &leaves[found->end];
        ++hi;
    }
    if (lo < hi) {
        auto const edge = chain::script_range_hashes(leaves, lo, hi);
        REQUIRE(edge);
        answer.hashes = *edge;
    }
    return answer;
}

hash_digest prefix_byte(uint8_t byte) {
    hash_digest prefix{};
    prefix[0] = byte;
    return prefix;
}

} // namespace

// Start Test Suite: script filter tests

TEST_CASE("script filter leaves parse serialize and stay in tree order", "[script filter]") {
    std::vector<chain::script_leaf> leaves;
    leaves.reserve(std::size(script_filter_leaf_hex));
    for (auto const hex : script_filter_leaf_hex) {
        auto const bytes = decode_base16(hex);
        REQUIRE(bytes);
        REQUIRE(bytes->size() == chain::script_leaf_size);

        auto const leaf = chain::parse_script_leaf(*bytes);
        REQUIRE(leaf);
        REQUIRE(std::ranges::equal(chain::to_data(*leaf), *bytes));
        leaves.push_back(*leaf);
    }
    REQUIRE(leaves.size() == 10);
    REQUIRE(std::is_sorted(leaves.begin(), leaves.end()));

    auto const bytes = decode_base16(script_filter_leaf_hex[0]);
    REQUIRE(bytes);
    REQUIRE(bytes->size() == chain::script_leaf_size);
    REQUIRE(chain::parse_script_leaf(std::span<uint8_t const>(bytes->data(), 40)) == std::unexpected(chain::script_range_error::leaf));

    auto bad_kind = *bytes;
    bad_kind[40] = 4;
    REQUIRE(chain::parse_script_leaf(bad_kind) == std::unexpected(chain::script_range_error::leaf));
}

TEST_CASE("script filter leaf roots match the published roots", "[script filter]") {
    auto const leaves = script_filter_leaves();
    REQUIRE(leaves.size() == 10);
    std::span<chain::script_leaf const> const all = leaves;

    for (auto const& item : script_filter_roots) {
        INFO("root of " << item.count << " leaves");
        auto const root = chain::script_leaves_root(all.first(item.count));
        REQUIRE(root == require_hash(item.root));
    }
}

TEST_CASE("script filter range hashes rebuild the ten leaf root", "[script filter]") {
    auto const leaves = script_filter_leaves();
    REQUIRE(leaves.size() == 10);
    std::span<chain::script_leaf const> const all = leaves;
    auto const root = chain::script_leaves_root(all);
    REQUIRE(script_filter_roots[std::size(script_filter_roots) - 1].count == 10);
    REQUIRE(root == require_hash(script_filter_roots[std::size(script_filter_roots) - 1].root));

    for (auto const& item : script_filter_ranges) {
        INFO(item.name);
        auto const hashes = chain::script_range_hashes(all, item.lo, item.end);
        REQUIRE(hashes);
        REQUIRE(hashes->size() == item.hash_count);
        for (size_t i = 0; i < item.hash_count; ++i) {
            INFO(item.name << " hash " << i);
            REQUIRE((*hashes)[i] == require_hash(item.hashes[i]));
        }

        auto const rebuilt = chain::script_range_root(
            10,
            static_cast<uint32_t>(item.lo),
            all.subspan(item.lo, item.end - item.lo),
            *hashes);
        REQUIRE(rebuilt);
        REQUIRE(*rebuilt == root);
    }

    // size_t cannot name a negative start. These are the empty, reversed,
    // and past-the-end runs.
    std::pair<size_t, size_t> const refused[] = {{3, 3}, {4, 3}, {0, 11}};
    for (auto const [lo, end] : refused) {
        INFO("refused run " << lo << " to " << end);
        REQUIRE(chain::script_range_hashes(all, lo, end) == std::unexpected(chain::script_range_error::range));
    }
}

TEST_CASE("script filter range root rebuilds every run of one to forty leaves", "[script filter]") {
    for (size_t n = 1; n <= 40; ++n) {
        std::vector<chain::script_leaf> leaves(n);
        for (size_t i = 0; i < n; ++i) {
            // Two leaves share a key. The key's first four bytes are that key number, big-endian.
            auto const value = static_cast<uint32_t>(i / 2);
            leaves[i].key[0] = static_cast<uint8_t>(value >> 24);
            leaves[i].key[1] = static_cast<uint8_t>(value >> 16);
            leaves[i].key[2] = static_cast<uint8_t>(value >> 8);
            leaves[i].key[3] = static_cast<uint8_t>(value);
            leaves[i].tx_index = static_cast<uint32_t>(i % 2);
        }

        std::span<chain::script_leaf const> const all = leaves;
        auto const root = chain::script_leaves_root(all);
        auto const count = static_cast<uint32_t>(n);
        for (size_t lo = 0; lo < n; ++lo) {
            for (size_t end = lo + 1; end <= n; ++end) {
                INFO("leaves " << n << " run " << lo << " to " << end);
                auto const hashes = chain::script_range_hashes(all, lo, end);
                REQUIRE(hashes);

                auto const run = all.subspan(lo, end - lo);
                auto const lo_index = static_cast<uint32_t>(lo);
                auto const got = chain::script_range_root(count, lo_index, run, *hashes);
                REQUIRE(got);
                REQUIRE(*got == root);

                auto extra = *hashes;
                extra.push_back(hash_digest{});
                REQUIRE(chain::script_range_root(count, lo_index, run, extra) == std::unexpected(chain::script_range_error::range));

                if ( ! hashes->empty()) {
                    auto const fewer = std::span<hash_digest const>(*hashes).first(hashes->size() - 1);
                    REQUIRE(chain::script_range_root(count, lo_index, run, fewer) == std::unexpected(chain::script_range_error::range));
                }
            }
        }
    }

    std::array<chain::script_leaf, 2> past{};
    REQUIRE(chain::script_range_root(3, 2, past, std::span<hash_digest const>{}) == std::unexpected(chain::script_range_error::range));
}

TEST_CASE("script filter range check accepts an honest prefix and refuses a bad proof", "[script filter]") {
    auto const leaves = script_filter_leaves();
    REQUIRE(leaves.size() == 10);
    std::span<chain::script_leaf const> const all = leaves;
    auto const root = chain::script_leaves_root(all);
    auto const prefix_5c = prefix_byte(0x5c);

    auto const every = honest_script_range(all, hash_digest{}, 0);
    REQUIRE(every.first == 0);
    REQUIRE(every.first + every.run.size() == 10);
    REQUIRE(chain::check_script_range(root, every.count, every.first, every.run, every.before, every.after, every.hashes, hash_digest{}, 0));

    auto const low_nibble = honest_script_range(all, prefix_byte(0x10), 4);
    REQUIRE(low_nibble.first == 0);
    REQUIRE(low_nibble.first + low_nibble.run.size() == 2);
    REQUIRE(chain::check_script_range(root, low_nibble.count, low_nibble.first, low_nibble.run, low_nibble.before, low_nibble.after, low_nibble.hashes, prefix_byte(0x10), 4));

    auto const byte_5c = honest_script_range(all, prefix_5c, 8);
    REQUIRE(byte_5c.first == 4);
    REQUIRE(byte_5c.first + byte_5c.run.size() == 7);
    REQUIRE(chain::check_script_range(root, byte_5c.count, byte_5c.first, byte_5c.run, byte_5c.before, byte_5c.after, byte_5c.hashes, prefix_5c, 8));

    auto const byte_80 = honest_script_range(all, prefix_byte(0x80), 8);
    REQUIRE(byte_80.first == 7);
    REQUIRE(byte_80.first + byte_80.run.size() == 7);
    REQUIRE(chain::check_script_range(root, byte_80.count, byte_80.first, byte_80.run, byte_80.before, byte_80.after, byte_80.hashes, prefix_byte(0x80), 8));

    auto const byte_ff = honest_script_range(all, prefix_byte(0xff), 8);
    REQUIRE(byte_ff.first == 10);
    REQUIRE(byte_ff.first + byte_ff.run.size() == 10);
    REQUIRE(chain::check_script_range(root, byte_ff.count, byte_ff.first, byte_ff.run, byte_ff.before, byte_ff.after, byte_ff.hashes, prefix_byte(0xff), 8));

    auto const exact = honest_script_range(all, leaves[0].key, 256);
    REQUIRE(exact.first == 0);
    REQUIRE(exact.first + exact.run.size() == 2);
    REQUIRE(chain::check_script_range(root, exact.count, exact.first, exact.run, exact.before, exact.after, exact.hashes, leaves[0].key, 256));

    // The run's last matching leaf is presented as the leaf after the run, so it still carries the prefix.
    script_range_answer shorter{};
    shorter.count = 10;
    shorter.first = 4;
    shorter.run = all.subspan(4, 2);
    shorter.before = &leaves[3];
    shorter.after = &leaves[6];
    auto const shorter_hashes = chain::script_range_hashes(all, 3, 7);
    REQUIRE(shorter_hashes);
    shorter.hashes = *shorter_hashes;
    REQUIRE(chain::check_script_range(root, shorter.count, shorter.first, shorter.run, shorter.before, shorter.after, shorter.hashes, prefix_5c, 8) == std::unexpected(chain::script_range_error::leaves));

    // An empty run parked where the first matching leaf actually sits.
    script_range_answer absent{};
    absent.count = 10;
    absent.first = 4;
    absent.run = all.subspan(4, 0);
    absent.before = &leaves[3];
    absent.after = &leaves[4];
    auto const absent_hashes = chain::script_range_hashes(all, 3, 5);
    REQUIRE(absent_hashes);
    absent.hashes = *absent_hashes;
    REQUIRE(chain::check_script_range(root, absent.count, absent.first, absent.run, absent.before, absent.after, absent.hashes, prefix_5c, 8) == std::unexpected(chain::script_range_error::leaves));

    hash_digest other_root{{1}};
    REQUIRE(chain::check_script_range(other_root, byte_5c.count, byte_5c.first, byte_5c.run, byte_5c.before, byte_5c.after, byte_5c.hashes, prefix_5c, 8) == std::unexpected(chain::script_range_error::root));

    auto no_before = byte_5c;
    no_before.before = nullptr;
    REQUIRE(chain::check_script_range(root, no_before.count, no_before.first, no_before.run, no_before.before, no_before.after, no_before.hashes, prefix_5c, 8) == std::unexpected(chain::script_range_error::range));

    auto extra_after = every;
    extra_after.after = &leaves[0];
    REQUIRE(chain::check_script_range(root, extra_after.count, extra_after.first, extra_after.run, extra_after.before, extra_after.after, extra_after.hashes, hash_digest{}, 0) == std::unexpected(chain::script_range_error::range));

    auto unsorted = leaves;
    std::swap(unsorted[5], unsorted[8]);
    std::span<chain::script_leaf const> const unsorted_span = unsorted;
    auto const unsorted_root = chain::script_leaves_root(unsorted_span);
    auto const all_unsorted = honest_script_range(unsorted_span, hash_digest{}, 0);
    REQUIRE(chain::check_script_range(unsorted_root, all_unsorted.count, all_unsorted.first, all_unsorted.run, all_unsorted.before, all_unsorted.after, all_unsorted.hashes, hash_digest{}, 0) == std::unexpected(chain::script_range_error::leaves));

    // 0x5c uses the low four bits, which are past a prefix of four bits.
    REQUIRE(chain::check_script_range(root, byte_5c.count, byte_5c.first, byte_5c.run, byte_5c.before, byte_5c.after, byte_5c.hashes, prefix_5c, 4) == std::unexpected(chain::script_range_error::prefix));
    REQUIRE(chain::script_leaves_with_prefix(all, hash_digest{}, 257) == std::unexpected(chain::script_range_error::prefix));

    REQUIRE(chain::check_script_range(hash_digest{}, 0, 0, {}, nullptr, nullptr, {}, prefix_5c, 8));
    REQUIRE(chain::check_script_range(root, 0, 0, {}, nullptr, nullptr, {}, prefix_5c, 8) == std::unexpected(chain::script_range_error::root));
}
