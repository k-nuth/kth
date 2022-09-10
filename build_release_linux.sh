#!/bin/bash

mkdir -p build_release
cd build_release
conan install .. -s build_type=Release

cmake .. -GNinja -DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations \
                 -DCMAKE_VERBOSE_MAKEFILE=ON \
                 -DUSE_CONAN=ON \
                 -DCURRENCY=BCH \
                 -DBINLOG=OFF \
                 -DLOG_LIBRARY=spdlog \
                 -DWITH_CONSOLE_CAPI=ON \
                 -DDB_READONLY_MODE=OFF \
                 -DENABLE_TESTS=OFF \
                 -DWITH_TESTS=OFF \
                 -DWITH_TOOLS=OFF \
                 -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF \
                 -DDB_NEW=ON \
                 -DDB_NEW_BLOCKS=OFF \
                 -DDB_NEW_FULL=OFF \
                 -DBUILD_NODE_EXE=ON \
                 -DBUILD_C_API=OFF \
                 -DENABLE_SHARED_CAPI=ON \
                 -DCMAKE_BUILD_TYPE=Release

ninja -j16
# cmake --build .

