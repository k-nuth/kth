# Browser runtime and transport controls: real WASM run in Chromium Workers by
# ci.py against loopback fixtures (fixtures.py).
foreach(control IN ITEMS stream cap eof select terminal)
  add_executable(browser_${control}_control ${control}-proof.cpp)
  target_link_libraries(browser_${control}_control PRIVATE network::network)
  target_link_options(browser_${control}_control PRIVATE
      -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
  set_target_properties(browser_${control}_control PROPERTIES SUFFIX ".js")
endforeach()

# header_sync and its C API against the scripted BCH regtest peer.
if(TARGET c-api)
  add_executable(browser_headers_control headers-proof.cpp)
  target_link_libraries(browser_headers_control PRIVATE c-api::c-api)
  target_link_options(browser_headers_control PRIVATE
      -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
  set_target_properties(browser_headers_control PROPERTIES SUFFIX ".js")

  # The header cache across client lifetimes.
  add_executable(browser_persist_control persist-proof.cpp)
  target_link_libraries(browser_persist_control PRIVATE node::node)
  target_link_options(browser_persist_control PRIVATE
      -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
  set_target_properties(browser_persist_control PROPERTIES SUFFIX ".js")

  # Several peers and endpoint discovery.
  add_executable(browser_peers_control peers-proof.cpp)
  target_link_libraries(browser_peers_control PRIVATE node::node)
  target_link_options(browser_peers_control PRIVATE
      -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
  set_target_properties(browser_peers_control PROPERTIES SUFFIX ".js")

  # Block and transaction content sampling.
  add_executable(browser_content_control content-proof.cpp)
  target_link_libraries(browser_content_control PRIVATE c-api::c-api)
  target_link_options(browser_content_control PRIVATE
      -sENVIRONMENT=worker -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1
      -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=512MB)
  set_target_properties(browser_content_control PROPERTIES SUFFIX ".js")
endif()
