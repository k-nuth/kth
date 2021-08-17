#!/bin/bash

mkdir -p build_release
cd build_release
conan install .. -s build_type=Release

cmake .. -GNinja -DCMAKE_VERBOSE_MAKEFILE=ON \
                 -DUSE_CONAN=ON \
                 -DCURRENCY=BCH \
                 -DBINLOG=OFF \
                 -DWITH_CONSOLE_CAPI=ON \
                 -DDB_READONLY_MODE=OFF \
                 -DENABLE_TESTS=OFF \
                 -DWITH_TESTS=OFF \
                 -DWITH_TOOLS=OFF \
                 -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF \
                 -DDB_NEW=ON \
                 -DDB_NEW_BLOCKS=ON \
                 -DDB_NEW_FULL=ON \
                 -DBUILD_NODE_EXE=OFF \
                 -DBUILD_C_API=ON \
                 -DENABLE_SHARED_CAPI=ON \
                 -DCMAKE_BUILD_TYPE=Release

ninja -j4
# cmake --build .

