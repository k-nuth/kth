# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

cmake_minimum_required(VERSION 3.20)

project(secp256k1-turbo VERSION 0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_TEST "Compile with enable test." ON)
option(ENABLE_POSITION_INDEPENDENT_CODE "Enable POSITION_INDEPENDENT_CODE property" ON)
# Which vector backends to compile in. "auto" compiles every backend the
# compiler supports for the target and picks one at run time (CPUID), unless
# the whole build already targets that ISA (-march with IFMA), in which case
# the choice is static. "none" compiles no vector kernel: every call goes to
# libsecp256k1 (this is also what non-x86 targets, e.g. NEON, get).
set(KTH_SECP256K1_TURBO_BACKENDS "auto" CACHE STRING "Vector backends: auto | avx512ifma | none")
set_property(CACHE KTH_SECP256K1_TURBO_BACKENDS PROPERTY STRINGS auto avx512ifma none)

add_library(${PROJECT_NAME} STATIC
    src/batch_verify.cpp
    src/cpu.cpp
    src/deferred.cpp
    src/g_table.cpp
    src/kernel_scalar.cpp
    src/sha256.cpp
)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ${ENABLE_POSITION_INDEPENDENT_CODE})
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
# libsecp256k1 provides the per-signature fallback.
target_link_libraries(${PROJECT_NAME} PUBLIC secp256k1::secp256k1)

# The AVX-512 IFMA kernel lives in its own translation unit compiled with the
# AVX-512 flags; it is only called after a CPUID check, so the rest of the
# library keeps the project's baseline -march.
set(KTH_SECP256K1_TURBO_AVX512_FLAGS "")
if (NOT KTH_SECP256K1_TURBO_BACKENDS STREQUAL "none" AND CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64)" AND NOT MSVC)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx512f -mavx512ifma -mavx512vl -mavx512dq -mavx512bw" KTH_CXX_HAS_AVX512IFMA)
    if (KTH_CXX_HAS_AVX512IFMA)
        set(KTH_SECP256K1_TURBO_AVX512_FLAGS -mavx512f -mavx512ifma -mavx512vl -mavx512dq -mavx512bw)
        target_sources(${PROJECT_NAME} PRIVATE src/kernel_avx512ifma.cpp)
        set_source_files_properties(src/kernel_avx512ifma.cpp PROPERTIES COMPILE_OPTIONS "${KTH_SECP256K1_TURBO_AVX512_FLAGS}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE KTH_SECP256K1_TURBO_HAVE_AVX512IFMA=1)
        message(STATUS "Knuth: secp256k1-turbo AVX-512 IFMA backend enabled (runtime dispatched unless the target -march has IFMA)")
    elseif (KTH_SECP256K1_TURBO_BACKENDS STREQUAL "avx512ifma")
        message(FATAL_ERROR "Knuth: KTH_SECP256K1_TURBO_BACKENDS=avx512ifma but the compiler does not accept the AVX-512 IFMA flags")
    endif()
endif()
if (NOT KTH_SECP256K1_TURBO_AVX512_FLAGS)
    message(STATUS "Knuth: secp256k1-turbo AVX-512 IFMA backend not built for this target; libsecp256k1 fallback only")
endif()

# GPU backends. The device code (gpu/kernel_core.h) is shared: CUDA compiles
# it as device code, the Metal runtime compiles it from source at first use.
# "auto" builds every runtime whose toolchain is present (CUDA needs nvcc,
# Metal needs an Apple target); a device is still looked for at run time.
set(KTH_SECP256K1_TURBO_GPU "auto" CACHE STRING "GPU backends: auto | cuda | metal | none")
set_property(CACHE KTH_SECP256K1_TURBO_GPU PROPERTY STRINGS auto cuda metal none)
set(KTH_SECP256K1_TURBO_GPU_BUILT "")
if (KTH_SECP256K1_TURBO_GPU MATCHES "^(auto|cuda)$")
    include(CheckLanguage)
    check_language(CUDA)
    if (CMAKE_CUDA_COMPILER)
        if (NOT CMAKE_CUDA_STANDARD)
            set(CMAKE_CUDA_STANDARD 20)  # the host C++ standard (23) is newer than nvcc accepts
        endif()
        enable_language(CUDA)
        # "native" compiles for the GPU of the build machine; a distributed
        # build sets a list (e.g. "89;120").
        set(KTH_SECP256K1_TURBO_CUDA_ARCHITECTURES "native" CACHE STRING "CUDA architectures for the kernel")
        find_package(CUDAToolkit REQUIRED)
        target_sources(${PROJECT_NAME} PRIVATE gpu/gpu_cuda.cu)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON
            CUDA_ARCHITECTURES "${KTH_SECP256K1_TURBO_CUDA_ARCHITECTURES}")
        target_compile_definitions(${PROJECT_NAME} PUBLIC KTH_SECP256K1_TURBO_HAVE_CUDA=1)
        target_link_libraries(${PROJECT_NAME} PUBLIC CUDA::cudart_static)
        list(APPEND KTH_SECP256K1_TURBO_GPU_BUILT cuda)
        message(STATUS "Knuth: secp256k1-turbo CUDA backend enabled (architectures: ${KTH_SECP256K1_TURBO_CUDA_ARCHITECTURES})")
    elseif (KTH_SECP256K1_TURBO_GPU STREQUAL "cuda")
        message(FATAL_ERROR "Knuth: KTH_SECP256K1_TURBO_GPU=cuda but no CUDA compiler was found")
    endif()
endif()
if (APPLE AND KTH_SECP256K1_TURBO_GPU MATCHES "^(auto|metal)$")
    enable_language(OBJCXX)
    set(KTH_SECP256K1_TURBO_METAL_INC ${CMAKE_CURRENT_BINARY_DIR}/kernel_metal_source.inc)
    add_custom_command(
        OUTPUT ${KTH_SECP256K1_TURBO_METAL_INC}
        COMMAND ${CMAKE_COMMAND}
            -DCORE=${CMAKE_CURRENT_SOURCE_DIR}/gpu/kernel_core.h
            -DENTRY=${CMAKE_CURRENT_SOURCE_DIR}/gpu/kernel_metal.metal
            -DOUT=${KTH_SECP256K1_TURBO_METAL_INC}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gpu/embed.cmake
        DEPENDS gpu/kernel_core.h gpu/kernel_metal.metal gpu/embed.cmake
        COMMENT "Embedding the Metal kernel source")
    target_sources(${PROJECT_NAME} PRIVATE gpu/gpu_metal.mm ${KTH_SECP256K1_TURBO_METAL_INC})
    set_source_files_properties(gpu/gpu_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(${PROJECT_NAME} PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(${PROJECT_NAME} PUBLIC KTH_SECP256K1_TURBO_HAVE_METAL=1)
    target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Metal" "-framework Foundation")
    list(APPEND KTH_SECP256K1_TURBO_GPU_BUILT metal)
    message(STATUS "Knuth: secp256k1-turbo Metal backend enabled")
elseif (KTH_SECP256K1_TURBO_GPU STREQUAL "metal")
    message(FATAL_ERROR "Knuth: KTH_SECP256K1_TURBO_GPU=metal needs an Apple target")
endif()
if (KTH_SECP256K1_TURBO_GPU_BUILT)
    target_sources(${PROJECT_NAME} PRIVATE gpu/gpu_host.cpp)
else()
    message(STATUS "Knuth: secp256k1-turbo GPU backends not built")
endif()

# What was built, for whoever packages this library. The decision is made here
# — by whether a CUDA compiler was found, wherever it was found — and a
# packager that worked it out again from its own environment could reach a
# different answer: link the CUDA backend and then not tell its consumers
# about the runtime it needs, or point them at a toolkit that is not the one
# used. One line per fact, read by the recipe's package_info().
set(KTH_SECP256K1_TURBO_GPU_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/secp256k1-turbo-gpu.txt")
string(REPLACE ";" "," KTH_SECP256K1_TURBO_GPU_BUILT_CSV "${KTH_SECP256K1_TURBO_GPU_BUILT}")
set(KTH_SECP256K1_TURBO_CUDA_LIBDIR "")
if ("cuda" IN_LIST KTH_SECP256K1_TURBO_GPU_BUILT)
    set(KTH_SECP256K1_TURBO_CUDA_LIBDIR "${CUDAToolkit_LIBRARY_DIR}")
endif()
file(WRITE "${KTH_SECP256K1_TURBO_GPU_MANIFEST}"
    "backends=${KTH_SECP256K1_TURBO_GPU_BUILT_CSV}\ncuda_libdir=${KTH_SECP256K1_TURBO_CUDA_LIBDIR}\n")

if (ENABLE_TEST AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    enable_testing()
    find_package(Catch2 3 REQUIRED)
    find_package(GMP REQUIRED)
    find_package(nanobench REQUIRED)

    add_executable(kth_secp256k1_turbo_test
        test/field.cpp
        test/scalar.cpp
        test/kernel.cpp
        test/verify.cpp
        test/deferred.cpp
        test/gpu_core.cpp
    )
    if (KTH_SECP256K1_TURBO_AVX512_FLAGS)
        target_sources(kth_secp256k1_turbo_test PRIVATE test/field_avx512ifma.cpp)
        set_source_files_properties(test/field_avx512ifma.cpp PROPERTIES COMPILE_OPTIONS "${KTH_SECP256K1_TURBO_AVX512_FLAGS}")
        target_compile_definitions(kth_secp256k1_turbo_test PRIVATE KTH_SECP256K1_TURBO_HAVE_AVX512IFMA=1)
    endif()
    if (NOT KTH_SECP256K1_TURBO_GPU_BUILT)
        # test/gpu_core.cpp checks the device code against libsecp256k1 by
        # running it on the host, whether or not a GPU runtime was built. With
        # one, the host reference is part of the library; without, the test
        # brings it along, so the kernel is still covered where no GPU is.
        target_sources(kth_secp256k1_turbo_test PRIVATE gpu/gpu_host.cpp)
    endif()
    target_include_directories(kth_secp256k1_turbo_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gpu)
    target_compile_definitions(kth_secp256k1_turbo_test PRIVATE KTH_SECP256K1_TURBO_CHECK_LIMBS=1)
    target_link_libraries(kth_secp256k1_turbo_test PRIVATE ${PROJECT_NAME} Catch2::Catch2WithMain gmp::gmp)
    add_test(NAME kth_secp256k1_turbo_test COMMAND kth_secp256k1_turbo_test)

    add_executable(kth_secp256k1_turbo_bench bench/benchmarks.cpp)
    target_include_directories(kth_secp256k1_turbo_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(kth_secp256k1_turbo_bench PRIVATE ${PROJECT_NAME} nanobench::nanobench)
endif()

include(GNUInstallDirs)
# Installed when the library is part of what is being packaged: built on its
# own, or as the node's signature verification. Otherwise nothing links it, and
# a build of the node's own targets does not produce the archive an install
# rule here would then fail to find.
if (NOT DEFINED KTH_SIGNATURE_VERIFICATION OR KTH_SIGNATURE_VERIFICATION STREQUAL "secp256k1_turbo")
    install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES "${KTH_SECP256K1_TURBO_GPU_MANIFEST}" DESTINATION ${CMAKE_INSTALL_DATADIR}/kth)
    install(EXPORT ${PROJECT_NAME}-targets
        FILE ${PROJECT_NAME}-config.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    )
endif()
