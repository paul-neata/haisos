add_executable(AgentQuery.integrationtest AgentQueryTest.cpp)
target_include_directories(AgentQuery.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(AgentQuery.integrationtest PRIVATE
    Factory
    ServicesCreator
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
    AgentListRunningTool
    nlohmann_json::nlohmann_json
)
target_compile_features(AgentQuery.integrationtest PRIVATE cxx_std_17)
