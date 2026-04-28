add_executable(Factory.unittests FactoryTest.cpp)
target_link_libraries(Factory.unittests PRIVATE gtest_main gmock Factory)
target_compile_features(Factory.unittests PRIVATE cxx_std_17)
