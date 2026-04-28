add_executable(AgentStop.integrationtest AgentStopTest.cpp)
target_include_directories(AgentStop.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AgentStop.integrationtest PRIVATE
    Factory
    Agent
    LLMCommunicator
    HTTPClient
    Console
    ToolFactory
    Logger
    GetCurrentDateTime
    AgentStartTool
    AgentWaitToFinishTool
    AgentQueryTool
    AgentStopTool
    AgentListRunningTool
    nlohmann_json::nlohmann_json
)
target_compile_features(AgentStop.integrationtest PRIVATE cxx_std_17)
