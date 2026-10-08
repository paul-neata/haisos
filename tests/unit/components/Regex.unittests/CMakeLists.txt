add_executable(Regex.unittests RegexSyntaxTest.cpp RegexMatchTest.cpp)
target_link_libraries(Regex.unittests PRIVATE gtest_main Regex)
target_compile_features(Regex.unittests PRIVATE cxx_std_17)