add_executable(Hsh.unittests HshLexerTest.cpp HshParserTest.cpp HshPatternTest.cpp HshGlobTest.cpp HshArithmeticTest.cpp HshVariablesTest.cpp HshExpansionTest.cpp HshInvocationTest.cpp HshShellTest.cpp HshRedirectionTest.cpp)
target_include_directories(Hsh.unittests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/components/Factory
    ${CMAKE_SOURCE_DIR}/tests/unit/components/BuiltinCommands.unittests
)
target_link_libraries(Hsh.unittests PRIVATE gtest_main BuiltinCommands Environment Factory)
target_compile_features(Hsh.unittests PRIVATE cxx_std_17)
