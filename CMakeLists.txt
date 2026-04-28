cmake_minimum_required(VERSION 3.14)

project(haisos)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Read version from VERSION file
file(READ "${CMAKE_SOURCE_DIR}/HAISOS_VERSION" PROJECT_VERSION)
string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
add_compile_definitions(HAISOS_VERSION="${PROJECT_VERSION}")

# Platform detection (must be before output directory settings)
if(WIN32)
    set(PLATFORM_NAME "windows")
    set(PLATFORM_WINDOWS ON)
    set(PLATFORM_LINUX OFF)
    set(PLATFORM_WASM OFF)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    message(STATUS "Detected platform: Windows")
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "linux")
    set(PLATFORM_WINDOWS OFF)
    set(PLATFORM_LINUX ON)
    set(PLATFORM_WASM OFF)
    message(STATUS "Detected platform: Linux")
elseif(EMSCRIPTEN)
    set(PLATFORM_NAME "wasm")
    set(PLATFORM_WINDOWS OFF)
    set(PLATFORM_LINUX OFF)
    set(PLATFORM_WASM ON)
    message(STATUS "Detected platform: WASM/Emscripten")
else()
    set(PLATFORM_NAME "generic")
    set(PLATFORM_WINDOWS OFF)
    set(PLATFORM_LINUX OFF)
    set(PLATFORM_WASM OFF)
    message(WARNING "Unknown platform, defaulting to generic")
endif()

# Handle debug mode
option(HAISOS_DEBUG "Build in debug mode" OFF)
if(HAISOS_DEBUG)
    set(PLATFORM_NAME "${PLATFORM_NAME}_debug")
endif()

# Output directory - platform-specific subfolder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/${PLATFORM_NAME})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/${PLATFORM_NAME})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output/${PLATFORM_NAME})

# Prevent multi-config generators (e.g. MSVC) from adding Debug/Release subfolders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

# Build temp directory is controlled by the -B flag passed to cmake

# Add repo root as include directory so sources can use root-relative paths
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/extern/nlohmann_json/include)

# Add extern subdirectories for dependencies
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/extern/nlohmann_json)
add_subdirectory(${CMAKE_SOURCE_DIR}/extern/googletest)

# Find libcurl on Linux
if(PLATFORM_LINUX)
    find_package(CURL REQUIRED)
    message(STATUS "Found libcurl: ${CURL_LIBRARIES}")
endif()

enable_testing()

# Components subdirectories
add_subdirectory(src/components/Logger)
add_subdirectory(src/components/Factory)
add_subdirectory(src/components/Console)
add_subdirectory(src/components/HTTPClient)
add_subdirectory(src/components/LLMCommunicator)
add_subdirectory(src/components/ToolFactory)
add_subdirectory(src/components/HaisosEngine)
add_subdirectory(src/components/Agent)
add_subdirectory(src/tools/get_current_date_time)
add_subdirectory(src/tools/agent_start)
add_subdirectory(src/tools/agent_wait_to_finish)
add_subdirectory(src/tools/agent_query)
add_subdirectory(src/tools/agent_stop)
add_subdirectory(src/tools/agent_list_running)
add_subdirectory(src/haisos)
add_subdirectory(tests)
