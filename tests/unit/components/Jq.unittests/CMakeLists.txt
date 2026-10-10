add_executable(Jq.unittests
    JqInterpreterTest.cpp
    JqJsonReaderTest.cpp
    JqJsonWriterTest.cpp
    JqLexerTest.cpp
    JqParserTest.cpp
    JqValueTest.cpp
)
target_link_libraries(Jq.unittests PRIVATE gtest_main BuiltinCommands)
target_compile_features(Jq.unittests PRIVATE cxx_std_17)
