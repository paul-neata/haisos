add_library(Agent STATIC Agent.cpp AgentMessageBuffer.cpp)
target_include_directories(Agent PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Agent ${CMAKE_SOURCE_DIR}/src/components/LLMCommunicator ${CMAKE_SOURCE_DIR}/src/components/Console ${CMAKE_SOURCE_DIR}/src/components/ToolFactory ${CMAKE_SOURCE_DIR}/src/components/Logger ${CMAKE_SOURCE_DIR}/src/components/libheaders)
target_link_libraries(Agent PUBLIC LLMCommunicator Console ToolFactory Logger)
target_compile_features(Agent PUBLIC cxx_std_17)
