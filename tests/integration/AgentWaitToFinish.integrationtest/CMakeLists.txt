add_executable(AgentWaitToFinish.integrationtest AgentWaitToFinishTest.cpp)
target_include_directories(AgentWaitToFinish.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AgentWaitToFinish.integrationtest PRIVATE
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
target_compile_features(AgentWaitToFinish.integrationtest PRIVATE cxx_std_17)
