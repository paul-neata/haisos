add_library(AgentStopTool STATIC AgentStopTool.cpp)
target_include_directories(AgentStopTool PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/tools/agent_stop ${CMAKE_SOURCE_DIR}/src/tools/agent_tools_common)
target_link_libraries(AgentStopTool PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(AgentStopTool PUBLIC cxx_std_17)
