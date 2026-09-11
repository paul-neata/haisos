add_executable(Call.get_current_date_time.integrationtest CallGetCurrentDateTimeTest.cpp)
target_include_directories(Call.get_current_date_time.integrationtest PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(Call.get_current_date_time.integrationtest PRIVATE
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
target_compile_features(Call.get_current_date_time.integrationtest PRIVATE cxx_std_17)
