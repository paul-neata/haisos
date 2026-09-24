add_library(OSStartProcessTool STATIC OSStartProcessTool.cpp)
target_include_directories(OSStartProcessTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/os_start_process)
target_link_libraries(OSStartProcessTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(OSStartProcessTool PUBLIC cxx_std_17)
