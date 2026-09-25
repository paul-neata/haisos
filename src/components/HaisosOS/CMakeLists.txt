# A process's file I/O, on its own so that runtimes living outside this
# component (the builtin commands) can give their processes one too.
add_library(ProcessFileIO STATIC ProcessFileIO.cpp)
target_include_directories(ProcessFileIO PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/HaisosOS
)
target_compile_features(ProcessFileIO PUBLIC cxx_std_17)

add_library(HaisosOS STATIC HaisosOS.cpp AgentProcess.cpp AgentInputLoop.cpp OSToolFactory.cpp LuaProcess.cpp)
target_include_directories(HaisosOS PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/HaisosOS
)
target_link_libraries(HaisosOS PUBLIC
    Logger
    Console
    Agent
    ProcessFileIO
    OSReadFileTool
    OSWriteFileTool
    OSListDirectoryTool
    OSStartProcessTool
    OSListProcessesTool
    nlohmann_json::nlohmann_json
    lua_lib
)
target_compile_features(HaisosOS PUBLIC cxx_std_17)
