add_library(Filesystem STATIC Filesystem.cpp)
target_include_directories(Filesystem PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/Filesystem
)
target_compile_features(Filesystem PUBLIC cxx_std_17)
