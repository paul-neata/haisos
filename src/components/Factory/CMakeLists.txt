add_library(Factory STATIC Factory.cpp)
target_include_directories(Factory PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/Factory)
target_link_libraries(Factory PUBLIC Console Environment Filesystem HTTPClient Logger ServicesCreator HaisosOS BuiltinCommands)
target_compile_features(Factory PUBLIC cxx_std_17)