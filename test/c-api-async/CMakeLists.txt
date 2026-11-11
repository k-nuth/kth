# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying file COPYING.
find_package(asio REQUIRED)
find_package(Threads REQUIRED)
add_executable(kth_capi_async_callbacks_test
    main.cpp
    ../../src/c-api/src/chain/chain_async.cpp
    ../../src/c-api/src/chain/chain_other.cpp
    ../../src/c-api/src/domain/chain/block_list.cpp)
target_compile_features(kth_capi_async_callbacks_test PRIVATE cxx_std_23)
target_compile_definitions(kth_capi_async_callbacks_test PRIVATE ASIO_STANDALONE ASIO_DISABLE_AWAITABLE_FRAME_RECYCLING KTH_LIB_STATIC)
target_include_directories(kth_capi_async_callbacks_test BEFORE PRIVATE stubs ../../src/c-api/include)
target_link_libraries(kth_capi_async_callbacks_test PRIVATE asio::asio Threads::Threads)
add_test(NAME kth_capi_async_callbacks COMMAND kth_capi_async_callbacks_test)
set_tests_properties(kth_capi_async_callbacks PROPERTIES TIMEOUT 30)
