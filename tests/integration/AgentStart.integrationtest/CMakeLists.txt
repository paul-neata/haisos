add_executable(AgentStart.integrationtest AgentStartTest.cpp)
target_include_directories(AgentStart.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AgentStart.integrationtest PRIVATE
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
target_compile_features(AgentStart.integrationtest PRIVATE cxx_std_17)
