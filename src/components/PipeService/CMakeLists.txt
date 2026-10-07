add_library(PipeService STATIC PipeService.cpp Pipe.cpp)
target_include_directories(PipeService PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/PipeService)
target_link_libraries(PipeService PUBLIC Logger)
target_compile_features(PipeService PUBLIC cxx_std_17)
