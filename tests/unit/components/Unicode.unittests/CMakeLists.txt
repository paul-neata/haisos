add_executable(Unicode.unittests UnicodeTest.cpp)
target_link_libraries(Unicode.unittests PRIVATE gtest_main Unicode)
target_compile_features(Unicode.unittests PRIVATE cxx_std_17)
