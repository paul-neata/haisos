add_library(OSReadFileTool STATIC OSReadFileTool.cpp)
target_include_directories(OSReadFileTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/os_read_file ${CMAKE_SOURCE_DIR}/src/tools/os_tools_common)
target_link_libraries(OSReadFileTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(OSReadFileTool PUBLIC cxx_std_17)
