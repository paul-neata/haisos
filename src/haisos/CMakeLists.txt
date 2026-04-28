add_executable(haisos main.cpp CliParser.cpp)
target_include_directories(haisos PRIVATE ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/haisos ${CMAKE_SOURCE_DIR}/src/components/Factory ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_link_libraries(haisos PRIVATE Factory Logger)
target_compile_features(haisos PRIVATE cxx_std_17)