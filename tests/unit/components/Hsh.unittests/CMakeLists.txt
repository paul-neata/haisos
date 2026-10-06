add_executable(Hsh.unittests HshLexerTest.cpp HshParserTest.cpp)
target_link_libraries(Hsh.unittests PRIVATE gtest_main BuiltinCommands)
target_compile_features(Hsh.unittests PRIVATE cxx_std_17)
