if(KTH_BROWSER_NODE)
add_executable(browser_configuration_control configuration-proof.cpp)
target_link_libraries(browser_configuration_control PRIVATE node::node)
target_link_options(browser_configuration_control PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1)
set_target_properties(browser_configuration_control PROPERTIES SUFFIX ".js")
foreach(control IN ITEMS stream cap eof select terminal)
  add_executable(browser_${control}_control ${control}-proof.cpp)
  target_link_libraries(browser_${control}_control PRIVATE network::network)
  target_link_options(browser_${control}_control PRIVATE
      -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
  set_target_properties(browser_${control}_control PROPERTIES SUFFIX ".js")
endforeach()
# Optional application boundary control built from the selected IDL profile.
if(KTH_HEADERS_GLUE_CPP AND KTH_HEADERS_PRE_JS)
  add_executable(browser_headers_api_control "${KTH_HEADERS_GLUE_CPP}")
  target_link_libraries(browser_headers_api_control PRIVATE c-api::c-api)
  target_compile_options(browser_headers_api_control PRIVATE
      -include kth/capi/node/header_sync.h)
  target_link_options(browser_headers_api_control PRIVATE
      --no-entry --pre-js "${KTH_HEADERS_PRE_JS}"
      -sMODULARIZE=1 -sEXPORT_NAME=createHeaders -sENVIRONMENT=worker
      -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=2GB
      "-sEXPORTED_FUNCTIONS=['_malloc','_free','_strlen']")
  set_target_properties(browser_headers_api_control PROPERTIES SUFFIX ".js")
endif()

add_executable(browser_headers_control headers-sync-proof.cpp)
target_link_libraries(browser_headers_control PRIVATE node::node)
target_link_options(browser_headers_control PRIVATE
    -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=2GB)
set_target_properties(browser_headers_control PROPERTIES SUFFIX ".js")

add_executable(browser_interop_control interop-proof.cpp)
target_link_libraries(browser_interop_control PRIVATE node::node)
target_link_options(browser_interop_control PRIVATE
    -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
set_target_properties(browser_interop_control PROPERTIES SUFFIX ".js")
add_executable(browser_lifecycle_control lifecycle-proof.cpp)
target_link_libraries(browser_lifecycle_control PRIVATE node::node)
target_link_options(browser_lifecycle_control PRIVATE
    -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
set_target_properties(browser_lifecycle_control PROPERTIES SUFFIX ".js")

endif()
if(KTH_LEGACY_API_CONTROL)
  add_executable(ordinary_wasm_api_control legacy-api-proof.cpp)
  target_link_libraries(ordinary_wasm_api_control PRIVATE c-api::c-api)
  target_link_options(ordinary_wasm_api_control PRIVATE -sENVIRONMENT=node -sEXIT_RUNTIME=1)
  set_target_properties(ordinary_wasm_api_control PROPERTIES SUFFIX ".js")
endif()
