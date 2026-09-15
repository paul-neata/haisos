add_library(Environment STATIC Environment.cpp)
target_include_directories(Environment PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Environment)
target_compile_features(Environment PUBLIC cxx_std_17)
