add_executable(CliParser.unittests CliParserTest.cpp ${CMAKE_SOURCE_DIR}/src/haisos/CliParser.cpp)
target_include_directories(CliParser.unittests PRIVATE ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_link_libraries(CliParser.unittests PRIVATE gtest_main gmock)
target_compile_features(CliParser.unittests PRIVATE cxx_std_17)
