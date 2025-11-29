// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <kth/domain/chain/transaction.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/utility/container_source.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/istream_reader.hpp>


using kth::data_chunk;
using kth::data_source;
using kth::istream_reader;
using kth::to_chunk;

TEST_CASE("[test_read_null_terminated_string_unlimited] 1") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string_unlimited(source);
    REQUIRE(ret == "ABC");
}

TEST_CASE("[test_read_null_terminated_string_unlimited] 2") {
    data_chunk data = {0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string_unlimited(source);
    REQUIRE(ret == "");
}

TEST_CASE("[test_read_null_terminated_string_unlimited] 3") {
    data_chunk data = {};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string_unlimited(source);
    REQUIRE(ret == "");
}

TEST_CASE("[test_read_null_terminated_string] 1") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 4);
    REQUIRE(bool(ret));
    REQUIRE(*ret == "ABC");
}

TEST_CASE("[test_read_null_terminated_string] 2") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 5);
    REQUIRE(bool(ret));
    REQUIRE(*ret == "ABC");
}

TEST_CASE("[test_read_null_terminated_string] 3") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 3);
    REQUIRE( ! bool(ret));
}

TEST_CASE("[test_read_null_terminated_string] 4") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 2);
    REQUIRE( ! bool(ret));
}

TEST_CASE("[test_read_null_terminated_string] 5") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 1);
    REQUIRE( ! bool(ret));
}

TEST_CASE("[test_read_null_terminated_string] 6") {
    data_chunk data = {'A', 'B', 'C', 0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 0);
    REQUIRE( ! bool(ret));
}

TEST_CASE("[test_read_null_terminated_string] 7") {
    data_chunk data = {0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 0);
    REQUIRE( ! bool(ret));
}

TEST_CASE("[test_read_null_terminated_string] 8") {
    data_chunk data = {0};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 1);
    REQUIRE(bool(ret));
    REQUIRE(*ret == "");
}

TEST_CASE("[test_read_null_terminated_string] 9") {
    data_chunk data = {};
    kth::data_source ds(data);
    kth::istream_reader source(ds);

    auto ret = read_null_terminated_string(source, 1);
    REQUIRE( ! bool(ret));
}
