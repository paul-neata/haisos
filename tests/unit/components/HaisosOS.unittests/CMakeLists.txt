add_executable(HaisosOS.unittests HaisosOSTest.cpp LuaProcessTest.cpp)
target_include_directories(HaisosOS.unittests PRIVATE ${CMAKE_SOURCE_DIR}/src/components/Factory ${CMAKE_SOURCE_DIR}/src/components/ServicesCreator)
target_link_libraries(HaisosOS.unittests PRIVATE gtest_main HaisosOS Factory ServicesCreator)
target_compile_features(HaisosOS.unittests PRIVATE cxx_std_17)
