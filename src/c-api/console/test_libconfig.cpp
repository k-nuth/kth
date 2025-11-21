// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>
#include <print>

#include <kth/capi/libconfig/libconfig.h>

int main(int argc, char* argv[]) {

    auto config = kth_libconfig_get();

    std::println("version:              {}", config.version);
    std::println("microarchitecture_id: {}", config.microarchitecture_id);
    std::println("currency:             {}", config.currency);
    std::println("mempool:              {}", config.mempool);
    // std::println("db_mode:              {}", config.db_mode);
    std::println("db_readonly:          {}", config.db_readonly);
    std::println("debug_mode:           {}", config.debug_mode);
    std::println("architecture:         {}", config.architecture);
    std::println("os_name:              {}", config.os_name);
    std::println("compiler_name:        {}", config.compiler_name);
    std::println("compiler_version:     {}", config.compiler_version);
    std::println("optimization_level:   {}", config.optimization_level);
    std::println("build_timestamp:      {}", config.build_timestamp);

    std::println("endianness:           {}", config.endianness);
    std::println("size_int:             {}", int(config.type_sizes.size_int));
    std::println("size_long:            {}", int(config.type_sizes.size_long));
    std::println("size_pointer:         {}", int(config.type_sizes.size_pointer));
}
