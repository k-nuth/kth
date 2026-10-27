cmake_minimum_required(VERSION 3.20)
project(test_kth_package)

find_package(kth REQUIRED)

# Exercise the native installed export independently of Conan's include paths.
add_subdirectory(native_export)

# Test individual targets
add_executable(test_individual test_individual.cpp)
target_link_libraries(test_individual 
    PRIVATE 
    kth::infrastructure
    kth::domain
    kth::secp256k1
)

# Test meta target
add_executable(test_meta test_meta.cpp)
target_link_libraries(test_meta 
    PRIVATE 
    kth::kth
)

# Print available targets for verification
message(STATUS "Testing KTH package targets:")
if(TARGET kth::infrastructure)
    message(STATUS "  ✓ kth::infrastructure - Available")
endif()
if(TARGET kth::domain)
    message(STATUS "  ✓ kth::domain - Available")
endif()
if(TARGET kth::consensus)
    message(STATUS "  ✓ kth::consensus - Available")
endif()
if(TARGET kth::secp256k1)
    message(STATUS "  ✓ kth::secp256k1 - Available")
endif()
if(TARGET kth::database)
    message(STATUS "  ✓ kth::database - Available")
endif()
if(TARGET kth::blockchain)
    message(STATUS "  ✓ kth::blockchain - Available")
endif()
if(TARGET kth::node)
    message(STATUS "  ✓ kth::node - Available")
endif()
if(TARGET kth::network)
    message(STATUS "  ✓ kth::network - Available")
endif()
if(TARGET kth::c-api)
    message(STATUS "  ✓ kth::c-api - Available")
endif()
if(TARGET kth::kth)
    message(STATUS "  ✓ kth::kth - Available (meta target)")
endif()
