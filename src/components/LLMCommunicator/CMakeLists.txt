add_library(LLMCommunicator STATIC LLMCommunicator.cpp)
target_include_directories(LLMCommunicator PUBLIC ${CMAKE_SOURCE_DIR}/interfaces ${CMAKE_SOURCE_DIR}/src/components/LLMCommunicator ${CMAKE_SOURCE_DIR}/src/components/HTTPClient ${CMAKE_SOURCE_DIR}/src/components/Logger)
target_link_libraries(LLMCommunicator PUBLIC HTTPClient Logger nlohmann_json::nlohmann_json)
target_compile_features(LLMCommunicator PUBLIC cxx_std_17)