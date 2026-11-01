// Real BCHN transport/protocol control. This is not full-chain validation.
#include <kth/network/peer_session.hpp>
#include <kth/network/protocols_coro.hpp>
#include <kth/node/full_node.hpp>
#include <kth/infrastructure/utility/browser_runtime.hpp>
#include <cstdio>
#include <stdexcept>
kth::browser::runtime owner;
using namespace kth;
using namespace kth::network;
using namespace kth::domain;
::asio::awaitable<raw_message> read_command(peer_session& peer, std::string_view command) {
    for (unsigned i=0; i!=32; ++i) {
        auto message = co_await peer.read_message_direct();
        if (!message) throw std::runtime_error(message.error().message());
        if (message->heading.command() == command) co_return std::move(*message);
        if (message->heading.command() == message::ping::command) {
            byte_reader reader(message->payload);
            auto ping = message::ping::from_data(reader, peer.negotiated_version());
            if (ping) co_await peer.send_direct(message::pong(ping->nonce()));
        }
    }
    throw std::runtime_error("unexpected message admission limit");
}
::asio::awaitable<void> session(unsigned generation) {
    auto executor = co_await ::asio::this_coro::executor;
    settings options(kth::domain::config::network::regtest);
    options.browser_bridge_url = "ws://127.0.0.1:18766";
    options.validate_checksum = true;
    options.user_agent = "/KTH-browser-transport-control/";
    options.channel_handshake_seconds = 5;
    auto connected = co_await async_connect(executor, "127.0.0.1", 19444, options, std::chrono::seconds(5));
    if (!connected) throw std::runtime_error(connected.error().message());
    auto peer = *connected;
    auto handshake = co_await perform_handshake_direct(*peer,
        make_handshake_config(options, 0, 0x123456780000ULL+generation));
    if (!handshake) throw std::runtime_error(handshake.error().message());
    std::printf("BCHN session=%u version/verack version=%u agent=%s\n", generation,
        handshake->negotiated_version, handshake->peer_version->user_agent().c_str());
    constexpr uint64_t nonce = 0x192837465ULL;
    auto sent = co_await peer->send_direct(message::ping(nonce));
    if (sent) throw std::runtime_error(sent.message());
    auto pong_message = co_await read_command(*peer, "pong");
    byte_reader pong_reader(pong_message.payload);
    auto pong = message::pong::from_data(pong_reader, peer->negotiated_version());
    if (!pong || pong->nonce()!=nonce) throw std::runtime_error("pong mismatch");
    std::puts("BCHN ping/pong nonce verified");
    auto genesis = node::full_node::get_genesis_block(kth::domain::config::network::regtest);
    co_await peer->send_direct(message::get_headers(hash_list{genesis.hash()}, null_hash));
    auto raw_headers = co_await read_command(*peer, "headers");
    byte_reader headers_reader(raw_headers.payload);
    auto headers = message::headers::from_data(headers_reader, peer->negotiated_version());
    if (!headers || headers->elements().empty() || !headers->is_sequential()) throw std::runtime_error("headers invalid");
    auto previous = genesis.hash();
    for (auto const& header: headers->elements()) {
        if (header.previous_block_hash()!=previous || header.check(chain::hash(header), false))
            throw std::runtime_error("context-free header validation failed");
        previous = chain::hash(header);
    }
    std::printf("BCHN headers=%zu linkage/PoW/context-free checks passed\n", headers->elements().size());
    auto request = message::get_data::create(hash_list{chain::hash(headers->elements().front())}, message::inventory::type_id::block);
    if (!request) throw std::runtime_error("getdata invalid");
    co_await peer->send_direct(*request);
    auto raw_block = co_await read_command(*peer, "block");
    byte_reader block_reader(raw_block.payload);
    auto block = message::block::from_data(block_reader, peer->negotiated_version());
    if (!block || chain::hash(block->header())!=chain::hash(headers->elements().front()) || block->check())
        throw std::runtime_error("context-free block validation failed");
    std::printf("BCHN block bytes=%zu context-free checks passed (no UTXO/script connection claim)\n", raw_block.payload.size());
    peer->stop();
}
::asio::awaitable<void> control() {
    co_await session(1);
    co_await session(2);
}
int main() {
    spdlog::set_level(spdlog::level::warn);
    ::asio::co_spawn(owner.context(), control(), [](std::exception_ptr error) {
        if (error) { try { std::rethrow_exception(error); } catch(std::exception const& e) { std::printf("INTEROP FAILED %s\n", e.what()); } }
        else std::puts("BCHN interop control joined, reconnect is a new session");
        owner.when_idle([] { std::printf("OWNER QUIESCENT timers=%zu streams=%zu\n",owner.timers(),owner.streams()); });
    });
    owner.wake();
}
