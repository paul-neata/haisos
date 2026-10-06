add_library(BuiltinCommands STATIC
    BuiltinCommand.cpp
    BuiltinCommands.cpp
    BuiltinConfigurator.cpp
    BuiltinProcess.cpp
    commands/cat/Cat.cpp
    commands/echo/Echo.cpp
    commands/ls/Ls.cpp
    commands/man/Man.cpp
    commands/mkdir/Mkdir.cpp
    commands/pwd/Pwd.cpp
    commands/wc/Wc.cpp
    commands/hsh/HshWord.cpp
    commands/hsh/HshLexer.cpp
    commands/hsh/HshAst.cpp
    commands/hsh/HshParser.cpp
    commands/hsh/HshPattern.cpp
    commands/hsh/HshGlob.cpp
    commands/hsh/HshArithmetic.cpp
    commands/hsh/HshVariables.cpp
    commands/hsh/HshExpansion.cpp
    commands/hsh/Hsh.cpp
    commands/hsh/HshInvocation.cpp
    commands/hsh/HshShell.cpp
    commands/hsh/HshBuiltins.cpp
)
target_include_directories(BuiltinCommands PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/BuiltinCommands
)
target_link_libraries(BuiltinCommands PUBLIC Logger Filesystem ProcessFileIO Unicode)
target_compile_features(BuiltinCommands PUBLIC cxx_std_17)
