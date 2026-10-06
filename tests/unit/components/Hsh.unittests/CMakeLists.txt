add_executable(Hsh.unittests HshLexerTest.cpp HshParserTest.cpp HshPatternTest.cpp HshGlobTest.cpp HshArithmeticTest.cpp HshVariablesTest.cpp HshExpansionTest.cpp)
target_link_libraries(Hsh.unittests PRIVATE gtest_main BuiltinCommands Environment)
target_compile_features(Hsh.unittests PRIVATE cxx_std_17)
