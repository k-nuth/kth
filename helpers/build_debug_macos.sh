#!/bin/bash

mkdir -p build_debug
cd build_debug
conan install .. -s build_type=Debug --update

cmake .. -GNinja -DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations -DCMAKE_VERBOSE_MAKEFILE=ON -DLOG_LIBRARY=spdlog -DWITH_CONSOLE_CAPI=OFF -DDB_READONLY_MODE=OFF -DENABLE_TESTS=OFF -DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF  -DDB_NEW=ON -DDB_NEW_BLOCKS=OFF -DDB_NEW_FULL=OFF -DCMAKE_BUILD_TYPE=Debug
# cmake .. -GNinja -DCMAKE_VERBOSE_MAKEFILE=ON -DBINLOG=OFF -DWITH_CONSOLE_CAPI=OFF -DDB_READONLY_MODE=OFF -DDB_NEW=ON -DDB_NEW_BLOCKS=ON -DENABLE_TESTS=OFF -DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF  -DDB_NEW=ON -DDB_NEW_BLOCKS=OFF -DDB_NEW_FULL=OFF -DCMAKE_BUILD_TYPE=Debug
# cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON -DWITH_CONSOLE_CAPI=OFF -DDB_READONLY_MODE=OFF  -DDB_NEW=ON  -DDB_NEW_BLOCKS=ON -DENABLE_TESTS=OFF -DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF -DCMAKE_BUILD_TYPE=Debug

ninja -j8
# cmake --build .
