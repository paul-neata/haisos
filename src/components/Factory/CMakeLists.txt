add_library(Factory STATIC Factory.cpp)
target_include_directories(Factory PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Factory)
target_link_libraries(Factory PUBLIC Console Filesystem Logger LLMCommunicator ToolFactory HaisosEngine Agent)
target_compile_features(Factory PUBLIC cxx_std_17)