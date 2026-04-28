add_executable(AgentListRunning.integrationtest AgentListRunningTest.cpp)
target_include_directories(AgentListRunning.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AgentListRunning.integrationtest PRIVATE
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
target_compile_features(AgentListRunning.integrationtest PRIVATE cxx_std_17)
