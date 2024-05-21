#!/bin/bash

rm -rf build
conan lock create conanfile.py --version="0.1" --update
conan lock create conanfile.py --version "0.1" --lockfile=conan.lock --lockfile-out=build/conan.lock
conan install conanfile.py --lockfile=build/conan.lock -of build --build=missing


cmake --preset conan-release \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DBINLOG=OFF \
         -DWITH_CONSOLE_CAPI=ON \
         -DBUILD_C_API=ON \
         -DDB_READONLY_MODE=OFF \
         -DENABLE_TESTS=OFF \
         -DWITH_TESTS=OFF \
         -DWITH_TOOLS=OFF \
         -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF \
         -DLOG_LIBRARY="spdlog" \
         -DGLOBAL_BUILD=ON \
         -DCMAKE_BUILD_TYPE=Release

cmake --build --preset conan-release -j4 \
         -DCMAKE_VERBOSE_MAKEFILE=ON \
         -DBINLOG=OFF \
         -DWITH_CONSOLE_CAPI=ON \
         -DBUILD_C_API=ON \
         -DDB_READONLY_MODE=OFF \
         -DENABLE_TESTS=OFF \
         -DWITH_TESTS=OFF \
         -DWITH_TOOLS=OFF \
         -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF \
         -DLOG_LIBRARY="spdlog" \
         -DGLOBAL_BUILD=ON \
         -DCMAKE_BUILD_TYPE=Release



# rm -rf build_release
# mkdir -p build_release
# cd build_release
# conan install .. -s build_type=Release --build=missing

# cmake .. -GNinja \
#          -DCMAKE_VERBOSE_MAKEFILE=ON \
#          -DBINLOG=OFF \
#          -DWITH_CONSOLE_CAPI=ON \
#          -DBUILD_C_API=ON \
#          -DDB_READONLY_MODE=OFF \
#          -DENABLE_TESTS=OFF \
#          -DWITH_TESTS=OFF \
#          -DWITH_TOOLS=OFF \
#          -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF \
#          -DDB_NEW=ON \
#          -DDB_NEW_BLOCKS=OFF \
#          -DDB_NEW_FULL=OFF \
#          -DLOG_LIBRARY="spdlog" \
#          -DCMAKE_BUILD_TYPE=Release

# ninja -j4


