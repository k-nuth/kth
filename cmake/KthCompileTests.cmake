# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Capture the configured compiler context for standalone syntax checks. SDK
# flags are normally added by CMake's compile rule, not INCLUDE_DIRECTORIES.
function(kth_compile_test_options target output)
  set(options "${CMAKE_CXX_FLAGS}\n")
  set(configurations DEBUG RELEASE RELWITHDEBINFO MINSIZEREL ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
  list(TRANSFORM configurations TOUPPER)
  list(REMOVE_DUPLICATES configurations)
  foreach(configuration IN LISTS configurations)
    string(TOUPPER "${configuration}" upper)
    string(APPEND options "$<$<CONFIG:${configuration}>:${CMAKE_CXX_FLAGS_${upper}}>\n")
  endforeach()

  if(CMAKE_SYSROOT_COMPILE)
    string(APPEND options "--sysroot=\"${CMAKE_SYSROOT_COMPILE}\"\n")
  elseif(CMAKE_SYSROOT)
    string(APPEND options "--sysroot=\"${CMAKE_SYSROOT}\"\n")
  endif()
  if(APPLE)
    if(CMAKE_OSX_SYSROOT)
      set(sdk "${CMAKE_OSX_SYSROOT}")
      if(NOT IS_ABSOLUTE "${sdk}")
        execute_process(COMMAND xcrun --sdk "${sdk}" --show-sdk-path
          OUTPUT_VARIABLE sdk OUTPUT_STRIP_TRAILING_WHITESPACE
          COMMAND_ERROR_IS_FATAL ANY)
      endif()
      string(APPEND options "-isysroot\n\"${sdk}\"\n")
    endif()
    foreach(architecture IN LISTS CMAKE_OSX_ARCHITECTURES)
      string(APPEND options "-arch\n\"${architecture}\"\n")
    endforeach()
    if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      string(APPEND options "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}\n")
    endif()
  endif()

  set(target_options "$<TARGET_PROPERTY:${target},COMPILE_OPTIONS>")
  string(APPEND options "$<$<BOOL:${target_options}>:\"$<JOIN:${target_options},\"\n\">\">\n")
  string(APPEND options "-I\"$<JOIN:$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>,\"\n-I\">\"\n")
  string(APPEND options "-D\"$<JOIN:$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>,\"\n-D\">\"\n")
  file(GENERATE OUTPUT "${output}" CONTENT "${options}" TARGET ${target})
endfunction()
