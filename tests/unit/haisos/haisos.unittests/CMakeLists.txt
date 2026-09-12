add_executable(CliParser.unittests
    CliParserTest.cpp
    HaisosFileParserTest.cpp
    HaisosFileSystemBuilderTest.cpp
    ${CMAKE_SOURCE_DIR}/src/haisos/CliParser.cpp
    ${CMAKE_SOURCE_DIR}/src/haisos/HaisosFileParser.cpp
    ${CMAKE_SOURCE_DIR}/src/haisos/HaisosFileSystemBuilder.cpp
)
target_include_directories(CliParser.unittests PRIVATE ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_link_libraries(CliParser.unittests PRIVATE gtest_main gmock Factory ServicesCreator)
target_compile_features(CliParser.unittests PRIVATE cxx_std_17)
