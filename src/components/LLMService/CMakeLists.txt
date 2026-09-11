add_library(LLMService STATIC LLMService.cpp)
target_include_directories(LLMService PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/LLMService)
target_link_libraries(LLMService PUBLIC ToolFactory)
target_compile_features(LLMService PUBLIC cxx_std_17)
