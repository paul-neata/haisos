add_library(ServicesCreator STATIC ServicesCreator.cpp)
target_include_directories(ServicesCreator PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/ServicesCreator)
target_link_libraries(ServicesCreator PUBLIC NetworkService FileSystemService LLMService)
target_compile_features(ServicesCreator PUBLIC cxx_std_17)
