add_executable(BuiltinCommands.unittests BuiltinCommandsTest.cpp ChmodPathsTest.cpp CpTest.cpp CutTest.cpp EnvTest.cpp BuiltinTextTest.cpp ManTest.cpp PrintfTest.cpp RmTest.cpp SeqTest.cpp SortTest.cpp TestTest.cpp UniqTest.cpp WcTest.cpp WhichSleepTrueFalseTest.cpp)
target_include_directories(BuiltinCommands.unittests PRIVATE ${CMAKE_SOURCE_DIR}/src/components/Factory)
target_link_libraries(BuiltinCommands.unittests PRIVATE gtest_main BuiltinCommands Factory)
target_compile_features(BuiltinCommands.unittests PRIVATE cxx_std_17)