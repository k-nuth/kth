# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying file COPYING.

find_package(asio REQUIRED)
find_package(Threads REQUIRED)
add_executable(kth_capi_mining_info_callback_test
    main.cpp
    ../../src/c-api/src/chain/chain_mining.cpp)
target_compile_features(kth_capi_mining_info_callback_test PRIVATE cxx_std_23)
target_compile_definitions(kth_capi_mining_info_callback_test PRIVATE
    ASIO_STANDALONE KTH_LIB_STATIC)
# Compile the production boundary and public C headers. Replace only its chain,
# synchronous wait and conversion dependencies with controlled test doubles.
target_include_directories(kth_capi_mining_info_callback_test BEFORE PRIVATE
    stubs ../../src/c-api/include)
target_link_libraries(kth_capi_mining_info_callback_test PRIVATE asio::asio Threads::Threads)
add_test(NAME kth_capi_mining_info_callback COMMAND kth_capi_mining_info_callback_test)
set_tests_properties(kth_capi_mining_info_callback PROPERTIES TIMEOUT 20)
