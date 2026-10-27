cmake_minimum_required(VERSION 3.22)
project(test_exported_secp256k1 LANGUAGES CXX)

include("${KTH_PACKAGE_FOLDER}/lib/cmake/secp256k1/secp256k1-targets.cmake")
get_target_property(secp256k1_type secp256k1::secp256k1 TYPE)
if(NOT secp256k1_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "The installed secp256k1 target must remain static")
endif()

add_executable(test_exported_secp256k1 ../test_exported_secp256k1.cpp)
target_link_libraries(test_exported_secp256k1 PRIVATE secp256k1::secp256k1)
set_target_properties(test_exported_secp256k1 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
