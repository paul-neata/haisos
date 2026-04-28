add_executable(Console.unittests VirtualConsoleTest.cpp)
target_link_libraries(Console.unittests PRIVATE gtest_main Console)
target_compile_features(Console.unittests PRIVATE cxx_std_17)
