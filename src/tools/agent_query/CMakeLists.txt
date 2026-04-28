add_library(AgentQueryTool STATIC AgentQueryTool.cpp)
target_include_directories(AgentQueryTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/agent_query ${CMAKE_SOURCE_DIR}/src/tools/agent_tools_common)
target_link_libraries(AgentQueryTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(AgentQueryTool PUBLIC cxx_std_17)
