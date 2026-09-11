add_library(NetworkService STATIC NetworkService.cpp)
target_include_directories(NetworkService PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/NetworkService)
target_compile_features(NetworkService PUBLIC cxx_std_17)
