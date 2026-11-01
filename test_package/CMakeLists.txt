cmake_minimum_required(VERSION 3.20)
project(test_kth_package)

find_package(kth REQUIRED)

if(KTH_EXPECT_CONSENSUS AND NOT TARGET kth::consensus)
    message(FATAL_ERROR "The installed package must export its enabled consensus component")
elseif(NOT KTH_EXPECT_CONSENSUS AND TARGET kth::consensus)
    message(FATAL_ERROR "The installed package unexpectedly exports disabled consensus")
endif()

# Exercise the native installed export independently of Conan's include paths.
add_subdirectory(native_export)

# Exercise GMP through the installed infrastructure target alone. Linking the
# meta target here could hide an incomplete infrastructure dependency list.
add_executable(test_big_number test_big_number.cpp)
target_link_libraries(test_big_number PRIVATE kth::infrastructure)

# Test individual targets
add_executable(test_individual test_individual.cpp)
target_link_libraries(test_individual 
    PRIVATE 
    kth::infrastructure
    kth::domain
    kth::secp256k1
)

# Compile public headers using only the installed package's exported targets.
# An object target also checks cross builds where test executables cannot run.
if(TARGET kth::database)
    add_library(test_installed_database_headers OBJECT test_installed_database_headers.cpp)
    target_link_libraries(test_installed_database_headers PRIVATE kth::database)
endif()

# Test meta target
add_executable(test_meta test_meta.cpp)
target_link_libraries(test_meta 
    PRIVATE 
    kth::kth
)

# rpc=capnproto: the installed node component must carry the capnp runtime, the
# generated schema header and the exported server symbols (shared builds
# included). Constructing and stopping a server, and calling the exported
# mapping function, forces the link against the node library itself.
if(KTH_EXPECT_CAPNPROTO_RPC)
    add_executable(test_capnp_rpc_consumer test_capnp_rpc_consumer.cpp)
    target_link_libraries(test_capnp_rpc_consumer PRIVATE kth::node)
endif()

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
