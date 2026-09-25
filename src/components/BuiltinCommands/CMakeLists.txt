add_library(BuiltinCommands STATIC
    BuiltinCommand.cpp
    BuiltinCommands.cpp
    BuiltinConfigurator.cpp
    BuiltinProcess.cpp
    commands/Cat.cpp
    commands/Echo.cpp
    commands/Ls.cpp
    commands/Mkdir.cpp
    commands/Pwd.cpp
)
target_include_directories(BuiltinCommands PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/BuiltinCommands
)
target_link_libraries(BuiltinCommands PUBLIC Logger Filesystem ProcessFileIO)
target_compile_features(BuiltinCommands PUBLIC cxx_std_17)
