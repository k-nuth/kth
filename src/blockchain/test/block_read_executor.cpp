// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>
#include <kth/infrastructure/utility/timer.hpp>
#include <cstdlib>
#include <fmt/format.h>
#include <dlfcn.h>
#include <unistd.h>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/posix/stream_descriptor.hpp>
#include <asio/use_future.hpp>
#include "reorg_chain_fixture.hpp"
#include "regtest_miner.hpp"

using namespace kth;

TEST_CASE("fetch_block leaves its caller executor responsive during disk I/O", "[.disk executor]") {
    auto const* descriptor = std::getenv("KTH_BLOCK_READ_PROGRESS_FD");
    if (!descriptor) {
        SKIP("run through kth_block_read_executor_test to install the disk gate");
    }
    auto const arm = reinterpret_cast<void (*)(char const*)>(dlsym(RTLD_DEFAULT, "kth_arm_block_read_gate"));
    REQUIRE(arm != nullptr);
    auto const progress = int(std::strtol(descriptor, nullptr, 10));
    test::chain_fixture fixture("block_read_executor");
    REQUIRE(fixture.created()); REQUIRE(fixture.start());
    auto const block = test::mine_block(domain::chain::block::genesis_regtest().hash(), 1,
        uint32_t(zulu_time()) - 1200, 0, {}, 0);
    REQUIRE(fixture.organizer().add_headers({domain::message::header(block.header())}).headers_added == 1);
    auto& chain = fixture.chain();
    auto const magic = to_little_endian(get_disk_magic(domain::config::network::regtest));
    database::block_store writer(chain.data_dir() / "blocks", magic);
    REQUIRE(writer.initialize());
    REQUIRE(writer.scan_block_positions([](int32_t, uint32_t, hash_digest const&) {}).clean());
    REQUIRE(writer.scan_undo_positions([](hash_digest const&) -> std::optional<hash_digest> {
        return std::nullopt;
    }).status == database::block_store::undo_scan_status::clean_eof);
    auto const pos = writer.save_block(block, 1);
    REQUIRE_FALSE(pos.is_null());
    auto const idx = chain.headers().find(block.hash());
    REQUIRE(idx != blockchain::header_index::null_index);
    chain.headers().set_block_pos(idx, int16_t(pos.file), pos.pos);
    chain.headers().add_status(idx, blockchain::header_status::have_data);
    ::asio::io_context caller;
    auto const file = chain.data_dir() / "blocks" / fmt::format("blk{:05}.dat", pos.file);
    auto const hash = block.hash();
    arm(file.string().c_str()); // hold only fread of this block file
    auto result = ::asio::co_spawn(caller, chain.fetch_block(hash), ::asio::use_future);
    auto const trigger_fd = int(std::strtol(std::getenv("KTH_BLOCK_READ_TRIGGER_FD"), nullptr, 10));
    ::asio::posix::stream_descriptor trigger(caller, trigger_fd);
    char byte;
    ::asio::async_read(trigger, ::asio::buffer(&byte, 1), [progress](std::error_code ec, size_t count) {
        CHECK_FALSE(ec); CHECK(count == 1);
        CHECK(::write(progress, "P", 1) == 1);
    });
    caller.run();
    auto const fetched = result.get();
    if (!fetched) {
        INFO(fetched.error().message());
    }
    REQUIRE(fetched);
    CHECK(fetched->first->hash() == block.hash());
}
