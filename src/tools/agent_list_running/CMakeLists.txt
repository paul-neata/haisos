add_library(AgentListRunningTool STATIC AgentListRunningTool.cpp)
target_include_directories(AgentListRunningTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/agent_list_running ${CMAKE_SOURCE_DIR}/src/tools/agent_tools_common)
target_link_libraries(AgentListRunningTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(AgentListRunningTool PUBLIC cxx_std_17)
