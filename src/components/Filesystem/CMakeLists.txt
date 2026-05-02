add_library(Filesystem STATIC)

if(WIN32)
    target_sources(Filesystem PRIVATE windows/WindowsFilesystem.cpp)
elseif(EMSCRIPTEN)
    target_sources(Filesystem PRIVATE linux/PosixFilesystem.cpp)
else()
    target_sources(Filesystem PRIVATE linux/PosixFilesystem.cpp)
endif()

target_include_directories(Filesystem PUBLIC
    ${CMAKE_SOURCE_DIR}/interfaces
    ${CMAKE_SOURCE_DIR}/src/components/Filesystem
)
target_compile_features(Filesystem PUBLIC cxx_std_17)
