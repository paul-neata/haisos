add_library(FileSystemService STATIC FileSystemService.cpp)
target_include_directories(FileSystemService PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/FileSystemService)
target_link_libraries(FileSystemService PUBLIC Filesystem)
target_compile_features(FileSystemService PUBLIC cxx_std_17)
