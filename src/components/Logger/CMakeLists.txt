add_library(Logger STATIC Logger.cpp)
target_include_directories(Logger PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_compile_features(Logger PUBLIC cxx_std_17)