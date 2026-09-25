add_executable(Filesystem.unittests FilesystemTest.cpp PhysicalFileSystemTest.cpp MountTest.cpp BuiltinFileTest.cpp StatTest.cpp DeviceFileSystemTest.cpp)
target_include_directories(Filesystem.unittests PRIVATE ${CMAKE_SOURCE_DIR}/src/components/Filesystem)
target_link_libraries(Filesystem.unittests PRIVATE gtest_main Filesystem)
target_compile_features(Filesystem.unittests PRIVATE cxx_std_17)
