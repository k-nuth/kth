// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

#include <filesystem>
#include <future>
#include <memory>

#include <kth/database.hpp>

using namespace kth::domain::chain;
using namespace kth::database;
using namespace kth::domain::wallet;
using namespace boost::system;
using namespace std::filesystem;

block read_block(const std::string hex) {
    auto const data = decode_base16(hex);
    REQUIRE(data);
    block result;
    REQUIRE(result.from_data(*data));
    return result;
}

#define DIRECTORY "data_base"

class data_base_setup_fixture {
public:
    data_base_setup_fixture() {
        error_code ec;
        remove_all(DIRECTORY, ec);
        REQUIRE(create_directories(DIRECTORY, ec));
    }
};

BOOST_FIXTURE_TEST_SUITE(data_base_tests, data_base_setup_fixture)

#define MAINNET_BLOCK1 \
    "010000006fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000982" \
    "051fd1e4ba744bbbe680e1fee14677ba1a3c3540bf7b1cdb606e857233e0e61bc6649ffff00" \
    "1d01e3629901010000000100000000000000000000000000000000000000000000000000000" \
    "00000000000ffffffff0704ffff001d0104ffffffff0100f2052a0100000043410496b538e8" \
    "53519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52da7589379515d4e0a60" \
    "4f8141781e62294721166bf621e73a82cbf2342c858eeac00000000"

#define MAINNET_BLOCK2 \
    "010000004860eb18bf1b1620e37e9490fc8a427514416fd75159ab86688e9a8300000000d5f" \
    "dcc541e25de1c7a5addedf24858b8bb665c9f36ef744ee42c316022c90f9bb0bc6649ffff00" \
    "1d08d2bd6101010000000100000000000000000000000000000000000000000000000000000" \
    "00000000000ffffffff0704ffff001d010bffffffff0100f2052a010000004341047211a824" \
    "f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073dee6c89064984f03385" \
    "237d92167c13e236446b417ab79a0fcae412ae3316b77ac00000000"

#define MAINNET_BLOCK3 \
    "01000000bddd99ccfda39da1b108ce1a5d70038d0a967bacb68b6b63065f626a0000000044f" \
    "672226090d85db9a9f2fbfe5f0f9609b387af7be5b7fbb7a1767c831c9e995dbe6649ffff00" \
    "1d05e0ed6d01010000000100000000000000000000000000000000000000000000000000000" \
    "00000000000ffffffff0704ffff001d010effffffff0100f2052a0100000043410494b9d3e7" \
    "6c5b1629ecf97fff95d7a4bbdac87cc26099ada28066c6ff1eb9191223cd897194a08d0c272" \
    "6c5747f1db49e8cf90e75dc3e3550ae9b30086f3cd5aaac00000000"

class data_base_accessor : public data_base {
public:
    data_base_accessor(settings const& settings)
        : data_base(settings) {}

    void push_all(block_const_ptr_list_const_ptr in_blocks, size_t first_height, dispatcher& dispatch, result_handler handler) {
        data_base::push_all(in_blocks, first_height, dispatch, handler);
    }

    void pop_above(block_const_ptr_list_ptr out_blocks, hash_digest const& fork_hash, dispatcher& dispatch, result_handler handler) {
        data_base::pop_above(out_blocks, fork_hash, dispatch, handler);
    }
};

static
code push_all_result(data_base_accessor& instance, block_const_ptr_list_const_ptr in_blocks, size_t first_height, dispatcher& dispatch) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec) {
        promise.set_value(ec);
    };
    instance.push_all(in_blocks, first_height, dispatch, handler);
    return promise.get_future().get();
}

static
code pop_above_result(data_base_accessor& instance, block_const_ptr_list_ptr out_blocks, hash_digest const& fork_hash, dispatcher& dispatch) {
    std::promise<code> promise;
    auto const handler = [&promise](code ec) {
        promise.set_value(ec);
    };
    instance.pop_above(out_blocks, fork_hash, dispatch, handler);
    return promise.get_future().get();
}

// End Test Suite
