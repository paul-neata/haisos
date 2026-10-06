add_library(BuiltinCommands STATIC
    BuiltinCommand.cpp
    BuiltinCommands.cpp
    BuiltinConfigurator.cpp
    BuiltinProcess.cpp
    commands/cat/Cat.cpp
    commands/echo/Echo.cpp
    commands/ls/Ls.cpp
    commands/mkdir/Mkdir.cpp
    commands/pwd/Pwd.cpp
)
target_include_directories(BuiltinCommands PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/BuiltinCommands
)
target_link_libraries(BuiltinCommands PUBLIC Logger Filesystem ProcessFileIO)
target_compile_features(BuiltinCommands PUBLIC cxx_std_17)
