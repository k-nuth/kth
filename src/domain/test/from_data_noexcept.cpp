// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <type_traits>
#include <utility>

#include <test_helpers.hpp>

#include <kth/domain/chain/light_block.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: from_data noexcept

namespace {

template <typename T, typename... Args>
inline constexpr bool from_data_noexcept =
    noexcept(T::from_data(std::declval<byte_reader&>(), std::declval<Args>()...));

hash_digest filled_hash(uint8_t seed) {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) {
        hash[i] = uint8_t(seed + i);
    }
    return hash;
}

constexpr uint32_t latest = message::version::level::maximum;

} // namespace

// Parsers proven not to throw: they read fixed-size fields through noexcept
// byte_reader primitives, construct trivially copyable values and return them
// in an `expect<T>` whose payload and error moves are nothrow.
// ----------------------------------------------------------------------------

static_assert(from_data_noexcept<chain::point, bool>);
static_assert(from_data_noexcept<chain::point>);
static_assert(from_data_noexcept<chain::output_point, bool>);   // inherited point::from_data
static_assert(from_data_noexcept<chain::header, bool>);
static_assert(from_data_noexcept<chain::header>);
static_assert(from_data_noexcept<message::header, uint32_t>);
static_assert(from_data_noexcept<message::verack, uint32_t>);
static_assert(from_data_noexcept<message::xverack, uint32_t>);
static_assert(from_data_noexcept<message::xversion, uint32_t>);
static_assert(from_data_noexcept<message::memory_pool, uint32_t>);
static_assert(from_data_noexcept<message::send_headers, uint32_t>);
static_assert(from_data_noexcept<message::send_addrv2, uint32_t>);
static_assert(from_data_noexcept<message::get_address, uint32_t>);
static_assert(from_data_noexcept<message::filter_clear, uint32_t>);
static_assert(from_data_noexcept<message::ping, uint32_t>);
static_assert(from_data_noexcept<message::pong, uint32_t>);
static_assert(from_data_noexcept<message::fee_filter, uint32_t>);
static_assert(from_data_noexcept<message::send_compact, uint32_t>);
static_assert(from_data_noexcept<message::inventory_vector, uint32_t>);
static_assert(noexcept(chain::token::encoding::detail::from_data(
    std::declval<byte_reader&>(), std::declval<chain::fungible&>(), true)));
static_assert(noexcept(read_hash(std::declval<byte_reader&>())));

// Payload moves are nothrow, so the `expect<T>` returned by those parsers never
// throws while being constructed.
static_assert(std::is_nothrow_move_constructible_v<expect<chain::point>>);
static_assert(std::is_nothrow_move_constructible_v<expect<chain::header>>);
static_assert(std::is_nothrow_move_constructible_v<expect<message::header>>);
static_assert(std::is_nothrow_move_constructible_v<expect<message::ping>>);
static_assert(std::is_nothrow_move_constructible_v<expect<message::pong>>);
static_assert(std::is_nothrow_move_constructible_v<expect<message::fee_filter>>);
static_assert(std::is_nothrow_move_constructible_v<expect<message::send_compact>>);
static_assert(std::is_nothrow_move_constructible_v<expect<message::inventory_vector>>);

// Constructors used directly by those parsers.
static_assert(std::is_nothrow_constructible_v<chain::point, hash_digest const&, uint32_t>);
// The two chain::header implementations expose different constructors; the
// annotated one is whichever the compiled `from_data` constructs from.
#if defined(KTH_USE_HEADER_MEMBERS)
static_assert(std::is_nothrow_constructible_v<chain::header,
    uint32_t, hash_digest const&, hash_digest const&, uint32_t, uint32_t, uint32_t>);
#else
static_assert(std::is_nothrow_constructible_v<chain::header,
    std::array<uint8_t, 80> const&>);
#endif
static_assert(std::is_nothrow_constructible_v<message::header, chain::header const&>);
static_assert(std::is_nothrow_constructible_v<message::ping, uint64_t>);
static_assert(std::is_nothrow_constructible_v<message::pong, uint64_t>);
static_assert(std::is_nothrow_constructible_v<message::fee_filter, uint64_t>);
static_assert(std::is_nothrow_constructible_v<message::send_compact, bool, uint64_t>);
static_assert(std::is_nothrow_constructible_v<message::inventory_vector, message::inventory_vector::type_id, hash_digest const&>);

// Parsers that can throw stay unannotated: each allocates (vector, string,
// data_chunk, reserve/push_back/resize) or reaches a call that can throw.
// ----------------------------------------------------------------------------

static_assert( ! from_data_noexcept<chain::block>);
static_assert( ! from_data_noexcept<chain::input, bool>);
static_assert( ! from_data_noexcept<chain::light_block, bool>);
static_assert( ! from_data_noexcept<chain::output, bool>);
static_assert( ! from_data_noexcept<chain::script, bool>);
static_assert( ! from_data_noexcept<chain::transaction, bool>);
static_assert( ! from_data_noexcept<machine::operation>);
static_assert( ! noexcept(chain::token::encoding::from_data(std::declval<byte_reader&>())));
static_assert( ! noexcept(chain::token::encoding::detail::from_data(
    std::declval<byte_reader&>(), std::declval<chain::non_fungible&>(), true)));
static_assert( ! noexcept(chain::token::encoding::detail::from_data(
    std::declval<byte_reader&>(), std::declval<chain::both_kinds&>(), true)));
static_assert( ! from_data_noexcept<message::address, uint32_t>);
static_assert( ! from_data_noexcept<message::addrv2, uint32_t>);
static_assert( ! from_data_noexcept<message::alert, uint32_t>);
static_assert( ! from_data_noexcept<message::alert_payload, uint32_t>);
static_assert( ! from_data_noexcept<message::block, uint32_t>);
static_assert( ! from_data_noexcept<message::block_transactions, uint32_t>);
static_assert( ! from_data_noexcept<message::compact_block, uint32_t>);
static_assert( ! from_data_noexcept<message::double_spend_proof, uint32_t>);
static_assert( ! from_data_noexcept<message::double_spend_proof::spender, uint32_t>);
static_assert( ! from_data_noexcept<message::filter_add, uint32_t>);
static_assert( ! from_data_noexcept<message::filter_load, uint32_t>);
static_assert( ! from_data_noexcept<message::get_block_transactions, uint32_t>);
static_assert( ! from_data_noexcept<message::get_blocks, uint32_t>);
static_assert( ! from_data_noexcept<message::get_data, uint32_t>);
static_assert( ! from_data_noexcept<message::get_headers, uint32_t>);
static_assert( ! from_data_noexcept<message::headers, uint32_t>);
static_assert( ! from_data_noexcept<message::heading, uint32_t>);
static_assert( ! from_data_noexcept<message::inventory, uint32_t>);
static_assert( ! from_data_noexcept<message::merkle_block, uint32_t>);
static_assert( ! from_data_noexcept<message::not_found, uint32_t>);
static_assert( ! from_data_noexcept<message::prefilled_transaction, uint32_t>);
static_assert( ! from_data_noexcept<message::reject, uint32_t>);
static_assert( ! from_data_noexcept<message::transaction, uint32_t>);
static_assert( ! from_data_noexcept<message::version, uint32_t>);
static_assert( ! noexcept(wallet::ec_public::from_data(std::declval<data_chunk const&>())));
static_assert( ! noexcept(wallet::stealth_address::from_data(std::declval<data_chunk const&>())));

// Runtime behavior of the annotated parsers.
// ----------------------------------------------------------------------------

TEST_CASE("from_data noexcept - empty payload messages", "[from_data_noexcept]") {
    data_chunk const empty;
    REQUIRE(from_data_chunk<message::verack>(empty, latest).has_value());
    REQUIRE(from_data_chunk<message::xverack>(empty, latest).has_value());
    REQUIRE(from_data_chunk<message::get_address>(empty, latest).has_value());
    REQUIRE(from_data_chunk<message::memory_pool>(empty, latest).has_value());
    REQUIRE(from_data_chunk<message::send_headers>(empty, latest).has_value());
    REQUIRE(from_data_chunk<message::send_addrv2>(empty, latest).has_value());
    REQUIRE(from_data_chunk<message::filter_clear>(empty, latest).has_value());
}

TEST_CASE("from_data noexcept - version gated messages report the same errors", "[from_data_noexcept]") {
    data_chunk const empty;
    auto const too_low = message::version::level::minimum;
    auto const memory_pool = from_data_chunk<message::memory_pool>(empty, too_low);
    REQUIRE( ! memory_pool.has_value());
    REQUIRE(memory_pool.error() == error::unsupported_version);
    auto const send_headers = from_data_chunk<message::send_headers>(empty, too_low);
    REQUIRE( ! send_headers.has_value());
    REQUIRE(send_headers.error() == error::version_too_low);
    auto const send_addrv2 = from_data_chunk<message::send_addrv2>(empty, too_low);
    REQUIRE( ! send_addrv2.has_value());
    REQUIRE(send_addrv2.error() == error::version_too_low);
    auto const filter_clear = from_data_chunk<message::filter_clear>(empty, too_low);
    REQUIRE( ! filter_clear.has_value());
    REQUIRE(filter_clear.error() == error::version_too_low);
}

TEST_CASE("from_data noexcept - xversion drains the reader", "[from_data_noexcept]") {
    data_chunk const payload{0x01, 0x02, 0x03};
    byte_reader reader(byte_span(payload.data(), payload.size()));
    REQUIRE(message::xversion::from_data(reader, latest).has_value());
    REQUIRE(reader.is_exhausted());
}

TEST_CASE("from_data noexcept - ping and pong", "[from_data_noexcept]") {
    message::ping const ping(0x0102030405060708ull);
    auto const wire = to_data_chunk(ping, latest);
    REQUIRE(wire.size() == 8);
    auto const parsed = from_data_chunk<message::ping>(wire, latest);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == ping);

    // Before BIP31 a ping has no nonce and consumes nothing.
    byte_reader before(byte_span(wire.data(), wire.size()));
    auto const old_ping = message::ping::from_data(before, message::version::level::minimum);
    REQUIRE(old_ping.has_value());
    REQUIRE(old_ping->nonce() == 0);
    REQUIRE(before.position() == 0);

    data_chunk const truncated(wire.begin(), wire.begin() + 7);
    auto const bad_ping = from_data_chunk<message::ping>(truncated, latest);
    REQUIRE( ! bad_ping.has_value());
    REQUIRE(bad_ping.error() == error::read_past_end_of_buffer);

    message::pong const pong(77u);
    auto const pong_wire = to_data_chunk(pong, latest);
    auto const parsed_pong = from_data_chunk<message::pong>(pong_wire, latest);
    REQUIRE(parsed_pong.has_value());
    REQUIRE(*parsed_pong == pong);
    data_chunk const pong_truncated(pong_wire.begin(), pong_wire.begin() + 3);
    auto const bad_pong = from_data_chunk<message::pong>(pong_truncated, latest);
    REQUIRE( ! bad_pong.has_value());
    REQUIRE(bad_pong.error() == error::read_past_end_of_buffer);
}

TEST_CASE("from_data noexcept - fee_filter", "[from_data_noexcept]") {
    message::fee_filter const filter(1000u);
    auto const wire = to_data_chunk(filter, latest);
    auto const parsed = from_data_chunk<message::fee_filter>(wire, latest);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == filter);
    auto const old = from_data_chunk<message::fee_filter>(wire, message::version::level::minimum);
    REQUIRE( ! old.has_value());
    REQUIRE(old.error() == error::version_too_low);
    data_chunk const truncated(wire.begin(), wire.end() - 1);
    auto const bad = from_data_chunk<message::fee_filter>(truncated, latest);
    REQUIRE( ! bad.has_value());
    REQUIRE(bad.error() == error::read_past_end_of_buffer);
}

TEST_CASE("from_data noexcept - send_compact", "[from_data_noexcept]") {
    message::send_compact const instance(true, 1u);
    auto const wire = to_data_chunk(instance, latest);
    REQUIRE(wire.size() == 9);
    auto const parsed = from_data_chunk<message::send_compact>(wire, latest);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == instance);

    data_chunk illegal = wire;
    illegal[0] = 2;
    auto const bad_mode = from_data_chunk<message::send_compact>(illegal, latest);
    REQUIRE( ! bad_mode.has_value());
    REQUIRE(bad_mode.error() == error::illegal_value);

    data_chunk const truncated(wire.begin(), wire.end() - 1);
    auto const bad = from_data_chunk<message::send_compact>(truncated, latest);
    REQUIRE( ! bad.has_value());
    REQUIRE(bad.error() == error::read_past_end_of_buffer);
    REQUIRE( ! from_data_chunk<message::send_compact>(data_chunk{}, latest).has_value());
}

TEST_CASE("from_data noexcept - inventory_vector", "[from_data_noexcept]") {
    message::inventory_vector const instance(message::inventory_vector::type_id::block, filled_hash(3));
    auto const wire = to_data_chunk(instance, latest);
    REQUIRE(wire.size() == 36);
    auto const parsed = from_data_chunk<message::inventory_vector>(wire, latest);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == instance);
    data_chunk const truncated(wire.begin(), wire.end() - 1);
    auto const bad = from_data_chunk<message::inventory_vector>(truncated, latest);
    REQUIRE( ! bad.has_value());
    REQUIRE(bad.error() == error::read_past_end_of_buffer);
    REQUIRE( ! from_data_chunk<message::inventory_vector>(data_chunk(3, 0), latest).has_value());
}

TEST_CASE("from_data noexcept - point wire and store formats", "[from_data_noexcept]") {
    chain::point const instance(filled_hash(9), 5u);

    auto const wire = to_data_chunk(instance, true);
    REQUIRE(wire.size() == 36);
    auto const parsed_wire = from_data_chunk<chain::point>(wire, true);
    REQUIRE(parsed_wire.has_value());
    REQUIRE(*parsed_wire == instance);

    auto const store = to_data_chunk(instance, false);
    REQUIRE(store.size() == 34);
    auto const parsed_store = from_data_chunk<chain::point>(store, false);
    REQUIRE(parsed_store.has_value());
    REQUIRE(*parsed_store == instance);

    // Inherited factory: output_point parses exactly like point.
    byte_reader reader(byte_span(wire.data(), wire.size()));
    auto const out = chain::output_point::from_data(reader, true);
    REQUIRE(out.has_value());
    REQUIRE(out->hash() == instance.hash());
    REQUIRE(out->index() == instance.index());

    // The store format maps the 16 bit sentinel to the null index.
    chain::point const null_point(filled_hash(9), chain::point::null_index);
    auto const null_store = to_data_chunk(null_point, false);
    auto const parsed_null = from_data_chunk<chain::point>(null_store, false);
    REQUIRE(parsed_null.has_value());
    REQUIRE(parsed_null->index() == chain::point::null_index);

    data_chunk const truncated_wire(wire.begin(), wire.end() - 1);
    auto const bad_wire = from_data_chunk<chain::point>(truncated_wire, true);
    REQUIRE( ! bad_wire.has_value());
    REQUIRE(bad_wire.error() == error::read_past_end_of_buffer);
    data_chunk const truncated_store(store.begin(), store.end() - 1);
    REQUIRE( ! from_data_chunk<chain::point>(truncated_store, false).has_value());
    REQUIRE( ! from_data_chunk<chain::point>(data_chunk(10, 0), true).has_value());
}

TEST_CASE("from_data noexcept - chain header and message header", "[from_data_noexcept]") {
    chain::header const instance(1u, filled_hash(1), filled_hash(2), 3u, 4u, 5u);
    auto const wire = to_data_chunk(instance, true);
    REQUIRE(wire.size() == 80);
    auto const parsed = from_data_chunk<chain::header>(wire, true);
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == instance);
    REQUIRE(parsed->version() == 1u);
    REQUIRE(parsed->timestamp() == 3u);
    REQUIRE(parsed->nonce() == 5u);

    data_chunk const truncated(wire.begin(), wire.end() - 1);
    auto const bad = from_data_chunk<chain::header>(truncated, true);
    REQUIRE( ! bad.has_value());
    REQUIRE(bad.error() == error::read_past_end_of_buffer);

    // The headers message appends a zero byte after every header.
    data_chunk with_trailer = wire;
    with_trailer.push_back(0x00);
    auto const message_header = from_data_chunk<message::header>(with_trailer, latest);
    REQUIRE(message_header.has_value());
    REQUIRE(chain::header(*message_header) == instance);

    // Canonical version carries no trailer.
    auto const canonical = from_data_chunk<message::header>(wire, message::version::level::canonical);
    REQUIRE(canonical.has_value());

    auto const missing_trailer = from_data_chunk<message::header>(wire, latest);
    REQUIRE( ! missing_trailer.has_value());
    REQUIRE(missing_trailer.error() == error::read_past_end_of_buffer);

    data_chunk bad_trailer = wire;
    bad_trailer.push_back(0x01);
    auto const nonzero = from_data_chunk<message::header>(bad_trailer, latest);
    REQUIRE( ! nonzero.has_value());
    REQUIRE(nonzero.error() == error::version_too_new);
}

TEST_CASE("from_data noexcept - fungible token amount", "[from_data_noexcept]") {
    data_chunk const wire{0xfd, 0x34, 0x12};
    byte_reader reader(byte_span(wire.data(), wire.size()));
    chain::fungible value{};
    REQUIRE(chain::token::encoding::detail::from_data(reader, value, false).has_value());
    REQUIRE(uint64_t(value.amount) == 0x1234u);

    byte_reader empty(byte_span(static_cast<uint8_t const*>(nullptr), 0));
    auto const bad = chain::token::encoding::detail::from_data(empty, value, false);
    REQUIRE( ! bad.has_value());
    REQUIRE(bad.error() == error::read_past_end_of_buffer);
}

TEST_CASE("from_data noexcept - read_hash", "[from_data_noexcept]") {
    auto const hash = filled_hash(7);
    byte_reader reader(byte_span(hash.data(), hash.size()));
    auto const read = read_hash(reader);
    REQUIRE(read.has_value());
    REQUIRE(*read == hash);
    auto const again = read_hash(reader);
    REQUIRE( ! again.has_value());
    REQUIRE(again.error() == error::read_past_end_of_buffer);
}

// End Test Suite
