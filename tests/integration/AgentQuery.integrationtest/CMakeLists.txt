add_executable(AgentQuery.integrationtest AgentQueryTest.cpp)
target_include_directories(AgentQuery.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AgentQuery.integrationtest PRIVATE
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
target_compile_features(AgentQuery.integrationtest PRIVATE cxx_std_17)
