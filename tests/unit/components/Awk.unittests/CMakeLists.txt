add_executable(Awk.unittests AwkLexerTest.cpp AwkParserTest.cpp AwkCommandTest.cpp)
target_include_directories(Awk.unittests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/components/Factory
    ${CMAKE_SOURCE_DIR}/tests/unit/components/BuiltinCommands.unittests
)
target_link_libraries(Awk.unittests PRIVATE gtest_main BuiltinCommands Environment Factory)
target_compile_features(Awk.unittests PRIVATE cxx_std_17)
