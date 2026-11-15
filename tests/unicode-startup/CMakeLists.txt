cmake_minimum_required(VERSION 3.25)
project(unicode_startup LANGUAGES CXX)
find_package(simdutf CONFIG REQUIRED)
set(root "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(unicode_startup main.cpp
    ${root}/src/infrastructure/src/unicode/unicode.cpp
    ${root}/src/infrastructure/src/unicode/unicode_streambuf.cpp
    ${root}/src/infrastructure/src/unicode/unicode_istream.cpp
    ${root}/src/infrastructure/src/unicode/unicode_ostream.cpp
    ${root}/src/infrastructure/src/unicode/console_streambuf.cpp)
target_include_directories(unicode_startup PRIVATE ${root}/src/infrastructure/include)
target_compile_features(unicode_startup PRIVATE cxx_std_23)
target_compile_definitions(unicode_startup PRIVATE KI_STATIC NOMINMAX)
target_link_libraries(unicode_startup PRIVATE simdutf::simdutf)
if(MSVC)
    target_compile_options(unicode_startup PRIVATE /utf-8)
endif()
