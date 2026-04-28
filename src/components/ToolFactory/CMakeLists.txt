add_library(ToolFactory STATIC ToolFactory.cpp)
target_include_directories(ToolFactory PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/ToolFactory ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_link_libraries(ToolFactory PUBLIC Logger GetCurrentDateTime AgentStartTool AgentWaitToFinishTool AgentQueryTool AgentStopTool AgentListRunningTool)
target_compile_features(ToolFactory PUBLIC cxx_std_17)
