add_executable(Jq.unittests JqLexerTest.cpp JqParserTest.cpp)
target_link_libraries(Jq.unittests PRIVATE gtest_main BuiltinCommands)
target_compile_features(Jq.unittests PRIVATE cxx_std_17)