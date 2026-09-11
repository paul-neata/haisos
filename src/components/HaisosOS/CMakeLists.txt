add_library(HaisosOS STATIC HaisosOS.cpp AgentProcess.cpp OSToolFactory.cpp LuaProcess.cpp)
target_include_directories(HaisosOS PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/HaisosOS
)
target_link_libraries(HaisosOS PUBLIC
    Logger
    OSReadFileTool
    OSWriteFileTool
    OSListDirectoryTool
    OSStartProcessTool
    OSListProcessesTool
    nlohmann_json::nlohmann_json
    lua_lib
)
target_compile_features(HaisosOS PUBLIC cxx_std_17)
