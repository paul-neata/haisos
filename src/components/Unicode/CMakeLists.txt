add_library(Unicode STATIC Unicode.cpp UnicodeTables.cpp)
target_include_directories(Unicode PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src/components/Unicode)
target_compile_features(Unicode PUBLIC cxx_std_17)
