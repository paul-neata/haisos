add_library(Console STATIC Console.cpp VirtualConsole.cpp)
target_include_directories(Console PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Console ${CMAKE_SOURCE_DIR}/src/components/Logger ${CMAKE_SOURCE_DIR}/src/components/libheaders)
target_link_libraries(Console PUBLIC Logger)
target_compile_features(Console PUBLIC cxx_std_17)