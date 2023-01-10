# Copyright (c) 2016-2023 Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


function(_add_c_compile_flag _Flag _Var)
    check_cxx_compiler_flag(${_Flag} ${_Var})
    if (${_Var})
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_Flag}" )
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_Flag}" )
    endif()
endfunction()

function(_add_cxx_compile_flag _Flag _Var)
    check_cxx_compiler_flag(${_Flag} ${_Var})
    if (${_Var})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_Flag}" )
    endif()
endfunction()

function(_add_link_flag _Flag _Var)
    check_cxx_compiler_flag(${_Flag} ${_Var})
    if (${_Var})
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_Flag}" )
        set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} ${_Flag}" )
    endif()
endfunction()


function(_check_has_decl header header_result symbol symbol_result)
    check_include_file(${header} ${header_result})
    if (${header_result})
        add_definitions(-D${header_result})
        check_cxx_symbol_exists(${symbol} ${header} ${symbol_result})
        if (${symbol_result})
            add_definitions(-D${symbol_result}=1)
        else()
            add_definitions(-D${symbol_result}=0)
        endif()
    endif()
endfunction()



# Build
#==============================================================================
function(_group_sources target sources_dir)
    file(GLOB_RECURSE _headers
            ${sources_dir}/include/*.h ${sources_dir}/include/*.hpp)
    target_sources(${target} PRIVATE ${_headers})

    get_target_property(sources ${target} SOURCES)
    foreach (source ${sources})
        get_filename_component(group ${source} ABSOLUTE)
        get_filename_component(group ${group} DIRECTORY)
        file(RELATIVE_PATH group "${sources_dir}" "${group}")
        if (group)
            if (MSVC)
                string(REPLACE "/" "\\" group "${group}")
            endif()
            source_group("${group}" FILES "${source}")
        endif()
    endforeach()

    set_target_properties(${target} PROPERTIES FOLDER "core")
endfunction()




# Tests
#==============================================================================
function(_add_tests target)
    if (ENABLE_SHARED)
        target_compile_definitions(${target} PRIVATE -DBOOST_TEST_DYN_LINK)
    endif()
    target_include_directories(${target} SYSTEM PUBLIC ${Boost_INCLUDE_DIR})
    target_link_libraries(${target} PUBLIC ${Boost_UNIT_TEST_FRAMEWORK_LIBRARY})

    foreach (_test_name ${ARGN})
        add_test(
                NAME test.core.${_test_name}
                COMMAND ${target}
                --run_test=${_test_name}
                --show_progress=no
                --detect_memory_leak=0
                --report_level=no
                --build_info=yes)
    endforeach()
endfunction()
