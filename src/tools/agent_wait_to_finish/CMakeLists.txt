add_library(AgentWaitToFinishTool STATIC AgentWaitToFinishTool.cpp)
target_include_directories(AgentWaitToFinishTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/agent_wait_to_finish ${CMAKE_SOURCE_DIR}/src/tools/agent_tools_common)
target_link_libraries(AgentWaitToFinishTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(AgentWaitToFinishTool PUBLIC cxx_std_17)
