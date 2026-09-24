add_library(SelfCloseTool STATIC SelfCloseTool.cpp)
target_include_directories(SelfCloseTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/self_close)
target_link_libraries(SelfCloseTool PUBLIC Logger nlohmann_json::nlohmann_json)
target_compile_features(SelfCloseTool PUBLIC cxx_std_17)
