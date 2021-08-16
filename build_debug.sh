#!/bin/bash

mkdir -p build_debug
cd build_debug
conan install .. -s build_type=Debug

cmake .. -GNinja -DCMAKE_CXX_FLAGS=-Wno-deprecated-declarations -DCMAKE_VERBOSE_MAKEFILE=ON -DLOG_LIBRARY=spdlog -DWITH_CONSOLE_CAPI=OFF -DDB_READONLY_MODE=OFF -DENABLE_TESTS=OFF -DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF  -DDB_NEW=ON -DDB_NEW_BLOCKS=OFF -DDB_NEW_FULL=OFF -DCMAKE_BUILD_TYPE=Debug
# cmake .. -GNinja -DCMAKE_VERBOSE_MAKEFILE=ON -DBINLOG=OFF -DWITH_CONSOLE_CAPI=OFF -DDB_READONLY_MODE=OFF -DDB_NEW=ON -DDB_NEW_BLOCKS=ON -DENABLE_TESTS=OFF -DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF  -DDB_NEW=ON -DDB_NEW_BLOCKS=OFF -DDB_NEW_FULL=OFF -DCMAKE_BUILD_TYPE=Debug
# cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON -DWITH_CONSOLE_CAPI=OFF -DDB_READONLY_MODE=OFF  -DDB_NEW=ON  -DDB_NEW_BLOCKS=ON -DENABLE_TESTS=OFF -DWITH_TESTS=OFF -DWITH_TOOLS=OFF -DENABLE_ECMULT_STATIC_PRECOMPUTATION=OFF -DCMAKE_BUILD_TYPE=Debug

ninja -j8
# cmake --build .





# /Library/Developer/CommandLineTools/usr/bin/c++ -Wno-deprecated-declarations -DENABLE_MODULE_RECOVERY=1 -DENABLE_MODULE_SCHNORR=1 -DHAVE___INT128=1 -DKI_STATIC -DKTH_CURRENCY_BCH -DKTH_LOG_LIBRARY_SPDLOG -DKTH_PROJECT_VERSION=\"-\" -DUSE_ASM_X86_64=1 -DUSE_FIELD_5X52=1 -DUSE_FIELD_INV_BUILTIN=1 -DUSE_NUM_NONE=1 -DUSE_SCALAR_4X64=1 -DUSE_SCALAR_INV_BUILTIN=1 -D_GLIBCXX_USE_CXX11_ABI=1 -I/Users/fernando/.conan/data/boost/1.76.0/_/_/package/83ec2ef94325dac24ad1c4e729ad30b6dcc2808d/include -I/Users/fernando/.conan/data/lmdb/0.9.24/kth/stable/package/535a6602071e3eca1ec75c28f93d07c63f88c632/include -I/Users/fernando/.conan/data/spdlog/1.8.5/_/_/package/5ab84d6acfe1f23c4fae0ab88f26e3a396351ac9/include -I/Users/fernando/.conan/data/algorithm/0.1.239/tao/stable/package/4840973d2444cde9f04ae865e38e3aaa6dc0f094/include -I/Users/fernando/.conan/data/gmp/6.2.1/_/_/package/a63c298310cf535d0477841588785ade0d4388a7/include -I/Users/fernando/.conan/data/zlib/1.2.11/_/_/package/d98fae1010d1fb9e7f79a1e8a72bbf129d8660a2/include -I/Users/fernando/.conan/data/bzip2/1.0.8/_/_/package/7fc5f5b34b9038fbd81d8c4edd0e5d12d311b628/include -I/Users/fernando/.conan/data/libbacktrace/cci.20210118/_/_/package/d98fae1010d1fb9e7f79a1e8a72bbf129d8660a2/include -I/Users/fernando/.conan/data/libiconv/1.16/_/_/package/d98fae1010d1fb9e7f79a1e8a72bbf129d8660a2/include -I/Users/fernando/.conan/data/fmt/7.1.3/_/_/package/5ab84d6acfe1f23c4fae0ab88f26e3a396351ac9/include -I../infrastructure/include -I../secp256k1/include -g  -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk -fPIC -DFMT_HEADER_ONLY=1 -DSPDLOG_FMT_EXTERNAL -DBOOST_STACKTRACE_ADDR2LINE_LOCATION=\"/usr/bin/addr2line\" -DBOOST_STACKTRACE_USE_ADDR2LINE -DBOOST_STACKTRACE_USE_BACKTRACE -DBOOST_STACKTRACE_USE_NOOP -DBOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED -std=gnu++17 -MD -MT infrastructure/CMakeFiles/kth-infrastructure.dir/src/config/authority.cpp.o -MF infrastructure/CMakeFiles/kth-infrastructure.dir/src/config/authority.cpp.o.d -o infrastructure/CMakeFiles/kth-infrastructure.dir/src/config/authority.cpp.o -c ../infrastructure/src/config/authority.cpp