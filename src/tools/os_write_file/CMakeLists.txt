add_library(OSWriteFileTool STATIC OSWriteFileTool.cpp)
target_include_directories(OSWriteFileTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/os_write_file ${CMAKE_SOURCE_DIR}/src/tools/os_tools_common)
target_link_libraries(OSWriteFileTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(OSWriteFileTool PUBLIC cxx_std_17)
