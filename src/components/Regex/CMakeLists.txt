add_library(Regex STATIC Regex.cpp RegexParser.cpp RegexTree.cpp)
target_include_directories(Regex PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/components/Regex)
target_compile_features(Regex PUBLIC cxx_std_17)