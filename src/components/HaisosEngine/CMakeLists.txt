add_library(HaisosEngine STATIC HaisosEngine.cpp)
target_include_directories(HaisosEngine PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/HaisosEngine ${CMAKE_SOURCE_DIR}/src/components/LLMCommunicator ${CMAKE_SOURCE_DIR}/src/components/Console ${CMAKE_SOURCE_DIR}/src/components/ToolFactory ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_link_libraries(HaisosEngine PUBLIC LLMCommunicator Console ToolFactory Logger)
target_compile_features(HaisosEngine PUBLIC cxx_std_17)