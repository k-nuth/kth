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
