// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <expected>
#include <stdexcept>
#include <concepts>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <test_helpers.hpp>

#include <kth/domain/chain/light_block.hpp>

using namespace kth;
using namespace kd;

// Start Test Suite: to_data noexcept

namespace {

template <typename T, typename... Args>
inline constexpr bool to_data_noexcept =
    noexcept(std::declval<T const&>().to_data(std::declval<byte_writer&>(), std::declval<Args>()...));

hash_digest filled_hash(uint8_t seed) {
    hash_digest hash{};
    for (size_t i = 0; i < hash.size(); ++i) {
        hash[i] = uint8_t(seed + i);
    }
    return hash;
}

constexpr uint32_t latest = message::version::level::maximum;

// Generic-contract fixtures for `chain::detail::write`.
struct nothrow_put {
    expect<void> to_data(byte_writer& writer, bool) const noexcept { return writer.write_byte(0xab); }
};
struct throwing_put {
    bool fail;
    expect<void> to_data(byte_writer& writer, bool) const {
        if (fail) {
            throw std::runtime_error("put failed");
        }
        return writer.write_byte(0xcd);
    }
};
// Result shapes that merely convert to `expect<void>` were accepted before
// and must keep compiling, but are not provably nothrow.
struct enum_error_put {
    std::expected<void, error::error_code_t> to_data(byte_writer&, bool) const {
        return std::unexpected(error::operation_failed);
    }
};
struct enum_error_nothrow_put {
    std::expected<void, error::error_code_t> to_data(byte_writer&, bool) const noexcept {
        return std::unexpected(error::operation_failed);
    }
};

// A trivially copyable type whose `operator&` is hostile: `write_packed` must
// never take its address with the built-in spelling.
struct hostile_address {
    uint32_t value;
    hostile_address const* operator&() const { throw 1; }
};

} // namespace

// byte_writer contract is covered in the infrastructure tests; here we pin the
// domain serializers.
// ----------------------------------------------------------------------------

// Fixed-size layouts: every field is written through noexcept writer primitives.
static_assert(to_data_noexcept<chain::point, bool>);
static_assert(to_data_noexcept<chain::output_point, bool>);   // inherited point::to_data
static_assert(to_data_noexcept<chain::header, bool>);
static_assert(to_data_noexcept<chain::script, bool>);
static_assert(to_data_noexcept<chain::input, bool>);
static_assert(noexcept(std::declval<machine::operation const&>().to_data(std::declval<byte_writer&>())));
static_assert(noexcept(std::declval<message::heading const&>().to_data(std::declval<byte_writer&>())));
static_assert(noexcept(std::declval<message::double_spend_proof::spender const&>().to_data(std::declval<byte_writer&>())));

static_assert(to_data_noexcept<message::header, uint32_t>);
static_assert(to_data_noexcept<message::ping, uint32_t>);
static_assert(to_data_noexcept<message::pong, uint32_t>);
static_assert(to_data_noexcept<message::fee_filter, uint32_t>);
static_assert(to_data_noexcept<message::send_compact, uint32_t>);
static_assert(to_data_noexcept<message::send_headers, uint32_t>);
static_assert(to_data_noexcept<message::send_addrv2, uint32_t>);
static_assert(to_data_noexcept<message::verack, uint32_t>);
static_assert(to_data_noexcept<message::xverack, uint32_t>);
static_assert(to_data_noexcept<message::filter_clear, uint32_t>);
static_assert(to_data_noexcept<message::get_address, uint32_t>);
static_assert(to_data_noexcept<message::memory_pool, uint32_t>);
static_assert(to_data_noexcept<message::inventory_vector, uint32_t>);
static_assert(to_data_noexcept<message::inventory, uint32_t>);
static_assert(to_data_noexcept<message::headers, uint32_t>);
static_assert(to_data_noexcept<message::get_blocks, uint32_t>);
static_assert(to_data_noexcept<message::get_block_transactions, uint32_t>);
static_assert(to_data_noexcept<message::filter_add, uint32_t>);
static_assert(to_data_noexcept<message::filter_load, uint32_t>);
static_assert(to_data_noexcept<message::alert, uint32_t>);
static_assert(to_data_noexcept<message::alert_payload, uint32_t>);
static_assert(to_data_noexcept<message::reject, uint32_t>);
static_assert(to_data_noexcept<message::version, uint32_t>);
static_assert(to_data_noexcept<message::address, uint32_t>);
static_assert(to_data_noexcept<message::double_spend_proof, uint32_t>);
static_assert(to_data_noexcept<message::merkle_block, uint32_t>);
static_assert(to_data_noexcept<message::addrv2, uint32_t>);
static_assert(noexcept(std::declval<message::addrv2_entry const&>().time()));
static_assert(noexcept(std::declval<message::addrv2_entry const&>().services()));
static_assert(noexcept(std::declval<message::addrv2_entry const&>().network()));
static_assert(noexcept(std::declval<message::addrv2_entry const&>().addr()));
static_assert(noexcept(std::declval<message::addrv2_entry const&>().port()));
static_assert(noexcept(std::declval<infrastructure::message::network_address const&>().to_data(
    std::declval<byte_writer&>(), uint32_t{}, true)));

// CashTokens payloads: concrete alternatives only write preallocated bytes.
static_assert(noexcept(chain::token::encoding::to_data(std::declval<byte_writer&>(), std::declval<chain::fungible const&>())));
static_assert(noexcept(chain::token::encoding::to_data(std::declval<byte_writer&>(), std::declval<chain::non_fungible const&>())));
static_assert(noexcept(chain::token::encoding::to_data(std::declval<byte_writer&>(), std::declval<chain::both_kinds const&>())));

// `token_data_t::data` is a public std::variant that can become valueless
// (see the regressions below). The wrapper guards that state before it
// computes or writes anything and reports `error::illegal_value`, so it, and
// every serializer that reaches it, is noexcept.
static_assert(noexcept(chain::token::encoding::to_data(std::declval<byte_writer&>(), std::declval<chain::token_data_t const&>())));
static_assert(to_data_noexcept<chain::output, bool>);
static_assert(to_data_noexcept<chain::transaction, bool>);
static_assert(noexcept(std::declval<chain::block const&>().to_data(std::declval<byte_writer&>())));
static_assert(to_data_noexcept<message::transaction, uint32_t>);
static_assert(to_data_noexcept<message::block, uint32_t>);
static_assert(to_data_noexcept<message::prefilled_transaction, uint32_t>);
static_assert(to_data_noexcept<message::compact_block, uint32_t>);
static_assert(to_data_noexcept<message::block_transactions, uint32_t>);

// Metadata helpers return scalars, not `expect`, so they have no way to report
// a valueless variant: they stay potentially throwing for direct calls.
static_assert( ! noexcept(chain::token::encoding::bitfield(std::declval<chain::token_data_t const&>())));
static_assert( ! noexcept(chain::token::encoding::serialized_size(std::declval<chain::token_data_t const&>())));
static_assert( ! noexcept(chain::token::encoding::serialized_size(std::declval<chain::token_data_opt const&>())));
static_assert( ! noexcept(std::declval<chain::output const&>().serialized_size(true)));
static_assert( ! noexcept(std::declval<chain::transaction const&>().serialized_size(true)));
static_assert( ! noexcept(std::declval<chain::block const&>().serialized_size()));

// Owning conveniences allocate: they must never be noexcept.
static_assert( ! noexcept(to_data_chunk(std::declval<message::ping const&>(), uint32_t{})));
static_assert( ! noexcept(std::declval<message::heading const&>().to_data()));
static_assert( ! noexcept(std::declval<machine::operation const&>().to_data()));
static_assert( ! noexcept(chain::token::encoding::to_data(std::declval<chain::fungible const&>())));
static_assert( ! noexcept(chain::token::encoding::to_data(std::declval<chain::token_data_t const&>())));

// Generic collection writer: noexcept follows the element's `to_data`, and the
// element must return `expect<void>`.
static_assert(noexcept(chain::detail::write(std::declval<byte_writer&>(), std::declval<std::vector<chain::input> const&>(), true)));
static_assert(noexcept(chain::detail::write(std::declval<byte_writer&>(), std::declval<std::vector<chain::output> const&>(), true)));
static_assert(noexcept(chain::detail::write(std::declval<byte_writer&>(), std::declval<std::vector<nothrow_put> const&>(), true)));
static_assert( ! noexcept(chain::detail::write(std::declval<byte_writer&>(), std::declval<std::vector<throwing_put> const&>(), true)));
// Convertible result shapes stay accepted but conservatively throwing, even
// when their `to_data` is itself noexcept.
static_assert( ! noexcept(chain::detail::write(std::declval<byte_writer&>(), std::declval<std::vector<enum_error_put> const&>(), true)));
static_assert( ! noexcept(chain::detail::write(std::declval<byte_writer&>(), std::declval<std::vector<enum_error_nothrow_put> const&>(), true)));
static_assert(chain::detail::nothrow_put_to_data_v<nothrow_put>);
static_assert( ! chain::detail::nothrow_put_to_data_v<enum_error_nothrow_put>);
static_assert( ! chain::detail::nothrow_put_to_data_v<throwing_put>);

// Runtime.
// ----------------------------------------------------------------------------

TEST_CASE("to_data noexcept - generic collection writer", "[to_data_noexcept]") {
    data_chunk buffer(4, 0);
    byte_writer writer(buffer);
    std::vector<nothrow_put> const puts(2);
    REQUIRE(chain::detail::write(writer, puts, true).has_value());
    REQUIRE(buffer == data_chunk{0x02, 0xab, 0xab, 0x00});

    byte_writer short_writer(std::span<uint8_t>(buffer).first(2));
    auto const r = chain::detail::write(short_writer, puts, true);
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::write_past_end_of_buffer);
    REQUIRE(short_writer.position() == 2u);

    // A throwing element is not swallowed or converted: the exception escapes
    // unchanged and the bytes written before it stay written.
    std::vector<throwing_put> const throwing{{false}, {true}};
    data_chunk out(8, 0);
    byte_writer again(out);
    try {
        (void)chain::detail::write(again, throwing, true);
        FAIL("expected the element exception to escape");
    } catch (std::runtime_error const& e) {
        REQUIRE(std::string(e.what()) == "put failed");
    }
    REQUIRE(out[0] == 0x02);
    REQUIRE(out[1] == 0xcd);

    std::vector<throwing_put> const quiet{{false}, {false}};
    byte_writer ok(out);
    REQUIRE(chain::detail::write(ok, quiet, true).has_value());
    REQUIRE(ok.position() == 3u);
}

TEST_CASE("to_data noexcept - collection writer keeps convertible result shapes", "[to_data_noexcept]") {
    data_chunk buffer(4, 0);
    byte_writer writer(buffer);
    std::vector<enum_error_put> const puts(1);
    auto const r = chain::detail::write(writer, puts, true);
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::operation_failed);
    // The count prefix was written before the element failed.
    REQUIRE(writer.position() == 1u);

    std::vector<enum_error_nothrow_put> const nothrow_puts(2);
    byte_writer other(buffer);
    auto const r2 = chain::detail::write(other, nothrow_puts, true);
    REQUIRE( ! r2.has_value());
    REQUIRE(r2.error() == error::operation_failed);
}

TEST_CASE("to_data noexcept - point wire and store layouts", "[to_data_noexcept]") {
    chain::point const instance(filled_hash(9), 5u);

    auto const wire = to_data_chunk(instance, true);
    REQUIRE(wire.size() == 36u);
    REQUIRE(wire[32] == 5u);
    REQUIRE(wire[33] == 0u);
    REQUIRE(from_data_chunk<chain::point>(wire, true).value() == instance);

    auto const store = to_data_chunk(instance, false);
    REQUIRE(store.size() == 34u);
    REQUIRE(from_data_chunk<chain::point>(store, false).value() == instance);

    chain::point const null_index_point(filled_hash(1), chain::point::null_index);
    auto const compact = to_data_chunk(null_index_point, false);
    REQUIRE(compact[32] == 0xff);
    REQUIRE(compact[33] == 0xff);
}

TEST_CASE("to_data noexcept - short buffers keep the error and position", "[to_data_noexcept]") {
    chain::point const instance(filled_hash(2), 7u);

    data_chunk buffer(35, 0xee);
    byte_writer writer(buffer);
    auto const r = instance.to_data(writer, true);
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::write_past_end_of_buffer);
    // The hash was written before the index failed.
    REQUIRE(writer.position() == 32u);
    REQUIRE(buffer[34] == 0xee);

    data_chunk tiny(3, 0);
    byte_writer tiny_writer(tiny);
    auto const ping_r = message::ping(1).to_data(tiny_writer, latest);
    REQUIRE( ! ping_r.has_value());
    REQUIRE(ping_r.error() == error::write_past_end_of_buffer);
    REQUIRE(tiny_writer.position() == 0u);
}

TEST_CASE("to_data noexcept - empty and null buffers", "[to_data_noexcept]") {
    byte_writer null_writer{std::span<uint8_t>{}};
    REQUIRE(message::verack{}.to_data(null_writer, latest).has_value());
    REQUIRE(message::filter_clear{}.to_data(null_writer, latest).has_value());
    REQUIRE(message::get_address{}.to_data(null_writer, latest).has_value());
    REQUIRE(message::memory_pool{}.to_data(null_writer, latest).has_value());
    REQUIRE(message::send_headers{}.to_data(null_writer, latest).has_value());
    REQUIRE(message::send_addrv2{}.to_data(null_writer, latest).has_value());
    REQUIRE(null_writer.position() == 0u);

    // Zero-length pre-bip31 ping body on a null buffer.
    REQUIRE(message::ping(1).to_data(null_writer, message::version::level::minimum).has_value());

    // A non-empty payload must fail rather than touch the null buffer.
    auto const r = message::pong(1).to_data(null_writer, latest);
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::write_past_end_of_buffer);
}

TEST_CASE("to_data noexcept - header layouts", "[to_data_noexcept]") {
    chain::header const instance(1u, filled_hash(1), filled_hash(40), 0x01020304u, 0x1d00ffffu, 0xdeadbeefu);
    auto const wire = to_data_chunk(instance, true);
    REQUIRE(wire.size() == 80u);
    // Version, timestamp and nonce are little-endian.
    REQUIRE(wire[0] == 1u);
    REQUIRE(wire[1] == 0u);
    REQUIRE(wire[68] == 0x04);
    REQUIRE(wire[71] == 0x01);
    REQUIRE(wire[76] == 0xef);
    REQUIRE(wire[79] == 0xde);
    REQUIRE(from_data_chunk<chain::header>(wire, true).value() == instance);

    // The p2p header message appends a zero transaction count except in the
    // canonical version.
    message::header const msg(instance);
    REQUIRE(to_data_chunk(msg, message::version::level::canonical).size() == 80u);
    auto const with_count = to_data_chunk(msg, latest);
    REQUIRE(with_count.size() == 81u);
    REQUIRE(with_count[80] == 0u);
}

TEST_CASE("to_data noexcept - version gated bodies", "[to_data_noexcept]") {
    message::ping const ping(0x0102030405060708ull);
    REQUIRE(to_data_chunk(ping, message::version::level::minimum).empty());
    auto const bytes = to_data_chunk(ping, message::version::level::bip31);
    REQUIRE(bytes == data_chunk{0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01});
    REQUIRE(from_data_chunk<message::ping>(bytes, message::version::level::bip31).value() == ping);

    message::pong const pong(1);
    REQUIRE(to_data_chunk(pong, latest).size() == 8u);

    message::fee_filter const fee(0x0a0bu);
    REQUIRE(to_data_chunk(fee, latest) == data_chunk({0x0b, 0x0a, 0, 0, 0, 0, 0, 0}));

    message::send_compact const compact(true, 2u);
    auto const compact_bytes = to_data_chunk(compact, latest);
    REQUIRE(compact_bytes.size() == 9u);
    REQUIRE(compact_bytes[0] == 1u);
    REQUIRE(from_data_chunk<message::send_compact>(compact_bytes, latest).value() == compact);
}

TEST_CASE("to_data noexcept - inventory and collections", "[to_data_noexcept]") {
    message::inventory_vector const first(message::inventory_vector::type_id::block, filled_hash(3));
    message::inventory_vector const second(message::inventory_vector::type_id::transaction, filled_hash(4));
    auto const inv = message::inventory::create(message::inventory_vector::list{first, second}).value();
    auto const bytes = to_data_chunk(inv, latest);
    REQUIRE(bytes.size() == 1u + 2u * 36u);
    REQUIRE(bytes[0] == 2u);
    REQUIRE(bytes[1] == 2u);    // block type, little-endian
    REQUIRE(bytes[37] == 1u);   // transaction type
    REQUIRE(from_data_chunk<message::inventory>(bytes, latest).value() == inv);

    message::get_blocks const request(hash_list{filled_hash(5), filled_hash(6)}, filled_hash(7));
    auto const request_bytes = to_data_chunk(request, latest);
    REQUIRE(request_bytes.size() == 4u + 1u + 2u * 32u + 32u);
    REQUIRE(from_data_chunk<message::get_blocks>(request_bytes, latest).value() == request);

    auto const add = message::filter_add::create(data_chunk{1, 2, 3}).value();
    REQUIRE(to_data_chunk(add, latest) == data_chunk({3, 1, 2, 3}));

    // Short buffer through a loop: the count prefix and the first element's
    // type were written when its hash ran out of room.
    data_chunk buffer(36, 0);
    byte_writer writer(buffer);
    auto const r = inv.to_data(writer, latest);
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::write_past_end_of_buffer);
    REQUIRE(writer.position() == 5u);
}

TEST_CASE("to_data noexcept - token payloads", "[to_data_noexcept]") {
    auto const bytes_of = [](auto const& payload) {
        data_chunk buffer(64, 0);
        byte_writer writer(buffer);
        REQUIRE(chain::token::encoding::to_data(writer, payload).has_value());
        buffer.resize(writer.position());
        return buffer;
    };

    REQUIRE(bytes_of(chain::fungible{chain::amount_t{5}}) == data_chunk{0x05});
    REQUIRE(bytes_of(chain::non_fungible{chain::capability_t::none, {}}).empty());
    REQUIRE(bytes_of(chain::non_fungible{chain::capability_t::none, {1, 2, 3}}) == data_chunk({3, 1, 2, 3}));
    // NFT part first, then the amount.
    REQUIRE(bytes_of(chain::both_kinds{chain::fungible{chain::amount_t{300}},
                                       chain::non_fungible{chain::capability_t::none, {9}}})
            == data_chunk({0x01, 0x09, 0xfd, 0x2c, 0x01}));

    data_chunk tiny(1, 0);
    byte_writer writer(tiny);
    auto const r = chain::token::encoding::to_data(writer, chain::non_fungible{chain::capability_t::none, {1, 2}});
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::write_past_end_of_buffer);
}

namespace {

struct throwing_commitment {
    operator chain::commitment_t() const { throw std::length_error("commitment"); }
};

constexpr uint8_t sentinel = 0xee;

// A genuinely valueless variant: a throwing conversion escapes `emplace`.
chain::token_data_t valueless_token() {
    chain::token_data_t token{};
    token.id = filled_hash(1);
    token.data = chain::fungible{chain::amount_t{1}};
    REQUIRE_THROWS_AS(token.data.emplace<chain::non_fungible>(chain::capability_t::none, throwing_commitment{}),
                      std::length_error);
    REQUIRE(token.data.valueless_by_exception());
    return token;
}

chain::output valueless_output() {
    return chain::output(1u, chain::script{}, chain::token_data_opt{valueless_token()});
}

chain::transaction tx_of(chain::output::list outs) {
    return chain::transaction(1u, 0u, chain::input::list{}, std::move(outs));
}

// A non-empty script obtained by parsing real P2PKH bytes.
chain::script parsed_script() {
    data_chunk bytes{0x76, 0xa9, 0x14};
    for (uint8_t i = 0; i < 20; ++i) bytes.push_back(uint8_t(0x30 + i));
    bytes.push_back(0x88);
    bytes.push_back(0xac);
    byte_reader reader(bytes);
    auto script = chain::script::from_data(reader, false);
    REQUIRE(script.has_value());
    REQUIRE(script->serialized_size(false) == bytes.size());
    return *script;
}

chain::output plain_output() {
    return chain::output(5u, parsed_script(), chain::token_data_opt{});
}

// One output of every token shape, plus a tokenless one.
chain::transaction valid_transaction() {
    auto const id = filled_hash(7);
    chain::input::list ins;
    ins.emplace_back(chain::output_point(filled_hash(2), 3u), parsed_script(), 7u);
    return chain::transaction(2u, 9u, std::move(ins), chain::output::list{
        plain_output(),
        chain::output(1u, parsed_script(), chain::make_fungible(id, 5)),
        chain::output(2u, parsed_script(), chain::make_non_fungible(id, chain::capability_t::mut, {1, 2, 3})),
        chain::output(3u, parsed_script(), chain::make_both(id, 300, chain::capability_t::minting, {9})),
        chain::output(4u, chain::script{}, chain::make_fungible(id, 6))});
}

message::prefilled_transaction prefilled(uint64_t index, chain::transaction const& tx) {
    auto r = message::prefilled_transaction::create(index, tx);
    REQUIRE(r.has_value());
    return *r;
}

chain::header test_header() {
    return chain::header{};
}

data_chunk token_bytes(chain::token_data_t const& token) {
    data_chunk buffer(chain::token::encoding::serialized_size(token), sentinel);
    byte_writer writer(buffer);
    REQUIRE(chain::token::encoding::to_data(writer, token).has_value());
    REQUIRE(writer.position() == buffer.size());
    return buffer;
}

data_chunk concat(hash_digest const& id, data_chunk tail) {
    data_chunk out(id.begin(), id.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

// Runs `obj.to_data(writer, args...)` and expects the exact serialized size to
// fit, and every shorter buffer length to report write_past_end_of_buffer.
template <typename T, typename... Args>
void check_canonical_and_truncated(T const& obj, size_t size, Args... args) {
    {
        data_chunk buffer(size, sentinel);
        byte_writer writer(buffer);
        REQUIRE(obj.to_data(writer, args...).has_value());
        REQUIRE(writer.position() == size);
    }
    for (size_t length = 0; length < size; ++length) {
        data_chunk buffer(length, sentinel);
        byte_writer writer(buffer);
        auto const r = obj.to_data(writer, args...);
        REQUIRE( ! r.has_value());
        REQUIRE(r.error() == error::write_past_end_of_buffer);
        REQUIRE(writer.position() <= length);
    }
}

// Runs `obj.to_data` over a sentinel buffer and expects `illegal_value`
// without throwing; returns how many bytes were written before the error.
template <typename T, typename... Args>
size_t check_illegal_value(T const& obj, Args... args) {
    data_chunk buffer(512, sentinel);
    byte_writer writer(buffer);
    expect<void> r;
    REQUIRE_NOTHROW(r = obj.to_data(writer, args...));
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::illegal_value);
    return writer.position();
}

} // namespace

TEST_CASE("to_data noexcept - token wrapper reports illegal_value on a valueless variant", "[to_data_noexcept]") {
    auto const token = valueless_token();

    // Nothing is written, not even the id, and the position does not move.
    data_chunk buffer(64, sentinel);
    byte_writer writer(buffer);
    expect<void> r;
    REQUIRE_NOTHROW(r = chain::token::encoding::to_data(writer, token));
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::illegal_value);
    REQUIRE(writer.position() == 0u);
    REQUIRE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == sentinel; }));

    // Same on a zero length buffer: the variant check wins over short-buffer.
    data_chunk empty;
    byte_writer empty_writer(empty);
    auto const r2 = chain::token::encoding::to_data(empty_writer, token);
    REQUIRE( ! r2.has_value());
    REQUIRE(r2.error() == error::illegal_value);
}

TEST_CASE("to_data noexcept - metadata helpers keep throwing on a valueless variant", "[to_data_noexcept]") {
    auto const token = valueless_token();
    REQUIRE_THROWS_AS(chain::token::encoding::bitfield(token), std::bad_variant_access);
    REQUIRE_THROWS_AS(chain::token::encoding::serialized_size(token), std::bad_variant_access);
    REQUIRE_THROWS_AS(chain::token::encoding::serialized_size(chain::token_data_opt{token}), std::bad_variant_access);
    // The allocating convenience sizes its buffer first, so it throws too.
    REQUIRE_THROWS_AS(chain::token::encoding::to_data(token), std::bad_variant_access);
    // As do the size queries of the containers that embed the token.
    auto const out = valueless_output();
    REQUIRE_THROWS_AS(out.serialized_size(true), std::bad_variant_access);
    REQUIRE_THROWS_AS(tx_of({out}).serialized_size(true), std::bad_variant_access);
}

TEST_CASE("to_data noexcept - output reports illegal_value before any write", "[to_data_noexcept]") {
    auto const out = valueless_output();
    for (bool wire : {true, false}) {
        data_chunk buffer(128, sentinel);
        byte_writer writer(buffer);
        expect<void> r;
        REQUIRE_NOTHROW(r = out.to_data(writer, wire));
        REQUIRE( ! r.has_value());
        REQUIRE(r.error() == error::illegal_value);
        // The store layout would otherwise start with a height prefix.
        REQUIRE(writer.position() == 0u);
        REQUIRE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t b) { return b == sentinel; }));
    }
}

TEST_CASE("to_data noexcept - output reports write_past_end at every truncated length", "[to_data_noexcept]") {
    auto const id = filled_hash(8);
    std::vector<chain::output> const outputs{
        plain_output(),
        chain::output(1u, parsed_script(), chain::make_fungible(id, 5)),
        chain::output(2u, parsed_script(), chain::make_non_fungible(id, chain::capability_t::mut, {1, 2, 3})),
        chain::output(3u, parsed_script(), chain::make_both(id, 300, chain::capability_t::minting, {9}))};

    for (auto const& out : outputs) {
        for (bool wire : {true, false}) {
            auto const size = out.serialized_size(wire);
            for (size_t length = 0; length < size; ++length) {
                data_chunk buffer(length, sentinel);
                byte_writer writer(buffer);
                auto const r = out.to_data(writer, wire);
                REQUIRE( ! r.has_value());
                REQUIRE(r.error() == error::write_past_end_of_buffer);
                REQUIRE(writer.position() <= length);
            }
            data_chunk buffer(size, sentinel);
            byte_writer writer(buffer);
            REQUIRE(out.to_data(writer, wire).has_value());
            REQUIRE(writer.position() == size);
        }
    }
}

TEST_CASE("to_data noexcept - transaction and block propagate illegal_value", "[to_data_noexcept]") {
    auto const tx = tx_of({plain_output(), valueless_output()});
    auto const first_output = plain_output().serialized_size(true);

    // Normal stream behavior: the prefix written before the bad output stays.
    //   wire:  version(4) + 0 inputs(1) + 2 outputs(1) + first output.
    //   store: 2 outputs(1) + first output (store layout, with height).
    REQUIRE(check_illegal_value(tx, true) == 6u + first_output);
    REQUIRE(check_illegal_value(tx, false) == 1u + plain_output().serialized_size(false));

    chain::block const block(test_header(), chain::transaction::list{tx});
    REQUIRE(check_illegal_value(block) == chain::header::satoshi_fixed_size() + 1u + 6u + first_output);

    // Messages: the nested result must not be swallowed.
    REQUIRE(check_illegal_value(message::transaction(tx), latest) == 6u + first_output);
    REQUIRE(check_illegal_value(message::block(block), latest) == chain::header::satoshi_fixed_size() + 1u + 6u + first_output);
    REQUIRE(check_illegal_value(prefilled(3, tx), latest) == 1u + 6u + first_output);

    hash_digest const hash = filled_hash(3);
    chain::transaction::list const good_then_bad{tx_of({}), tx};
    // hash(32) + count(1) + empty tx (4+1+1+4) + bad tx prefix.
    REQUIRE(check_illegal_value(message::block_transactions(hash, good_then_bad), latest) == 32u + 1u + 10u + 6u + first_output);

    message::compact_block const compact(test_header(), 1, {}, {prefilled(0, tx)});
    // header + nonce(8) + short ids(1) + prefilled count(1) + index(1) + bad tx prefix.
    REQUIRE(check_illegal_value(compact, latest) == chain::header::satoshi_fixed_size() + 8u + 1u + 1u + 1u + 6u + first_output);
}

TEST_CASE("to_data noexcept - wrappers propagate short buffers", "[to_data_noexcept]") {
    auto const tx = valid_transaction();
    auto const size = tx.serialized_size(true);
    check_canonical_and_truncated(tx, size, true);
    check_canonical_and_truncated(tx, tx.serialized_size(false), false);
    check_canonical_and_truncated(message::transaction(tx), message::transaction(tx).serialized_size(latest), latest);

    chain::block const block(test_header(), chain::transaction::list{tx, tx});
    check_canonical_and_truncated(block, block.serialized_size());
    message::block const wire_block(block);
    check_canonical_and_truncated(wire_block, wire_block.serialized_size(latest), latest);

    auto const prefilled_tx = prefilled(4, tx);
    check_canonical_and_truncated(prefilled_tx, prefilled_tx.serialized_size(latest), latest);

    message::block_transactions const block_txs(filled_hash(3), chain::transaction::list{tx, tx});
    check_canonical_and_truncated(block_txs, block_txs.serialized_size(latest), latest);

    message::compact_block const compact(test_header(), 1, {1, 2}, {prefilled_tx, prefilled(5, tx)});
    check_canonical_and_truncated(compact, compact.serialized_size(latest), latest);
}

TEST_CASE("to_data noexcept - valid token bytes are unchanged and round trip", "[to_data_noexcept]") {
    auto const id = filled_hash(4);

    auto const fungible = chain::make_fungible(id, 5);
    auto const nft = chain::make_non_fungible(id, chain::capability_t::none, {1, 2, 3});
    auto const minting = chain::make_non_fungible(id, chain::capability_t::minting, {});
    auto const both = chain::make_both(id, 300, chain::capability_t::mut, {9});

    REQUIRE(token_bytes(fungible) == concat(id, {0x10, 0x05}));
    REQUIRE(token_bytes(nft) == concat(id, {0x60, 3, 1, 2, 3}));
    REQUIRE(token_bytes(minting) == concat(id, {0x22}));
    // NFT part first, then the amount.
    REQUIRE(token_bytes(both) == concat(id, {0x71, 0x01, 0x09, 0xfd, 0x2c, 0x01}));

    for (auto const& token : {fungible, nft, minting, both}) {
        auto bytes = token_bytes(token);
        byte_reader reader(bytes);
        auto const parsed = chain::token::encoding::from_data(reader);
        REQUIRE(parsed.has_value());
        REQUIRE(*parsed == token);

        // The allocating convenience yields the same bytes.
        REQUIRE(chain::token::encoding::to_data(token) == bytes);

        // Embedded in an output, wire layout: value, varint(wrapper), 0xef, token.
        chain::output const out(7u, chain::script{}, chain::token_data_opt{token});
        data_chunk expected;
        for (int i = 0; i < 8; ++i) expected.push_back(i == 0 ? 7 : 0);
        expected.push_back(uint8_t(1 + bytes.size()));
        expected.push_back(chain::encoding::PREFIX_BYTE);
        expected.insert(expected.end(), bytes.begin(), bytes.end());

        data_chunk buffer(out.serialized_size(true), sentinel);
        byte_writer writer(buffer);
        REQUIRE(out.to_data(writer, true).has_value());
        REQUIRE(writer.position() == buffer.size());
        REQUIRE(buffer == expected);

        byte_reader out_reader(buffer);
        auto const out_parsed = chain::output::from_data(out_reader, true);
        REQUIRE(out_parsed.has_value());
        REQUIRE(out_parsed->token_data() == chain::token_data_opt{token});
        REQUIRE(out_parsed->value() == 7u);

        check_canonical_and_truncated(out, buffer.size(), true);
        check_canonical_and_truncated(out, out.serialized_size(false), false);
    }
}

TEST_CASE("to_data noexcept - transaction with every token kind round trips", "[to_data_noexcept]") {
    auto const tx = valid_transaction();
    data_chunk buffer(tx.serialized_size(true), sentinel);
    byte_writer writer(buffer);
    REQUIRE(tx.to_data(writer, true).has_value());
    REQUIRE(writer.position() == buffer.size());

    byte_reader reader(buffer);
    auto const parsed = chain::transaction::from_data(reader, true);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->outputs().size() == tx.outputs().size());
    for (size_t i = 0; i < tx.outputs().size(); ++i) {
        REQUIRE(parsed->outputs()[i].token_data() == tx.outputs()[i].token_data());
        REQUIRE(parsed->outputs()[i].value() == tx.outputs()[i].value());
    }
}

TEST_CASE("to_data noexcept - write_packed ignores overloaded operator&", "[to_data_noexcept]") {
    static_assert(std::is_trivially_copyable_v<hostile_address>);
    data_chunk buffer(4, 0);
    byte_writer writer(buffer);
    hostile_address const value{0x04030201u};
    REQUIRE(writer.write_packed(value).has_value());
    REQUIRE(writer.position() == 4u);
}

TEST_CASE("to_data noexcept - addrv2 layout and short buffers", "[to_data_noexcept]") {
    auto const entry = message::addrv2_entry::create(
        1u, 0x0du, message::bip155_network::ipv4, data_chunk{1, 2, 3, 4}, 0x208du).value();
    auto const instance = message::addrv2::create({entry}).value();

    REQUIRE(entry.time() == 1u);
    REQUIRE(entry.services() == 0x0du);
    REQUIRE(entry.network() == message::bip155_network::ipv4);
    REQUIRE(entry.addr() == data_chunk({1, 2, 3, 4}));
    REQUIRE(entry.port() == 0x208du);

    data_chunk const expected{
        0x01,                    // count
        0x01, 0x00, 0x00, 0x00,  // time, little-endian
        0x0d,                    // services varint
        0x01,                    // network id
        0x04,                    // address length
        1, 2, 3, 4,
        0x20, 0x8d};             // port, big-endian
    REQUIRE(instance.serialized_size(latest) == expected.size());
    REQUIRE(to_data_chunk(instance, latest) == expected);

    data_chunk shorter(expected.size() - 1, 0);
    byte_writer writer(shorter);
    auto const r = instance.to_data(writer, latest);
    REQUIRE( ! r.has_value());
    REQUIRE(r.error() == error::write_past_end_of_buffer);
    REQUIRE(writer.position() == expected.size() - 2);

    message::addrv2 const empty;
    REQUIRE(to_data_chunk(empty, latest) == data_chunk{0x00});
}

TEST_CASE("to_data noexcept - merkle_block layout and short buffers", "[to_data_noexcept]") {
    chain::header const header(1u, filled_hash(1), filled_hash(40), 0x01020304u, 0x1d00ffffu, 0xdeadbeefu);
    message::merkle_block const instance(header, 3u, hash_list{filled_hash(7)}, data_chunk{0x05});

    auto const bytes = to_data_chunk(instance, latest);
    REQUIRE(bytes.size() == 80u + 4u + 1u + 32u + 1u + 1u);
    REQUIRE(instance.serialized_size(latest) == bytes.size());
    REQUIRE(data_chunk(bytes.begin() + 80, bytes.begin() + 84) == data_chunk({3, 0, 0, 0}));
    REQUIRE(bytes[84] == 1u);
    REQUIRE(bytes[85] == 7u);
    REQUIRE(bytes[117] == 1u);
    REQUIRE(bytes[118] == 5u);

    for (size_t n : {size_t(0), size_t(79), size_t(83), size_t(117), bytes.size() - 1}) {
        data_chunk shorter(n, 0);
        byte_writer writer(shorter);
        auto const r = instance.to_data(writer, latest);
        REQUIRE( ! r.has_value());
        REQUIRE(r.error() == error::write_past_end_of_buffer);
        REQUIRE(writer.position() <= n);
    }
}
