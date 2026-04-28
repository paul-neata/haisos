add_library(AgentStartTool STATIC AgentStartTool.cpp)
target_include_directories(AgentStartTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/agent_start ${CMAKE_SOURCE_DIR}/src/tools/agent_tools_common)
target_link_libraries(AgentStartTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(AgentStartTool PUBLIC cxx_std_17)
