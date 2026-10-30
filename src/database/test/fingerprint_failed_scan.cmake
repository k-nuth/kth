# Run the actual CLI against a dependency that fails after delivering an entry.
execute_process(COMMAND "${TOOL}" "${CMAKE_CURRENT_BINARY_DIR}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE diagnostic)
if(status EQUAL 0)
    message(FATAL_ERROR "Failed scan returned success: ${output}")
endif()
if(output MATCHES "Fingerprint:")
    message(FATAL_ERROR "Failed scan emitted a partial fingerprint: ${output}")
endif()
if(NOT diagnostic MATCHES "failed to scan")
    message(FATAL_ERROR "Missing scan failure diagnostic: ${diagnostic}")
endif()
