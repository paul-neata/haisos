add_executable(Agent.unittests AgentTest.cpp)
target_link_libraries(Agent.unittests PRIVATE gtest_main gmock Agent Factory)
target_compile_features(Agent.unittests PRIVATE cxx_std_17)
