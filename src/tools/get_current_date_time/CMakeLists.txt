add_library(GetCurrentDateTime STATIC GetCurrentDateTime.cpp)
target_include_directories(GetCurrentDateTime PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/get_current_date_time ${CMAKE_SOURCE_DIR}/src/tools/agent_tools_common)
target_link_libraries(GetCurrentDateTime PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(GetCurrentDateTime PUBLIC cxx_std_17)