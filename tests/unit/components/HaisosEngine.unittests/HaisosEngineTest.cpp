#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include "HaisosEngine.h"
#include "src/components/Factory/Factory.h"

using namespace Haisos;

TEST(HaisosEngineTest, Construction) {
    Factory factory;
    auto engine = factory.CreateHaisosEngine(factory);
    EXPECT_NE(engine, nullptr);
}

TEST(HaisosEngineTest, RunWithNonExistentFile) {
    Factory factory;
    auto engine = factory.CreateHaisosEngine(factory);

    // Try to run with non-existent file - should not crash
    RunConfig config;
    config.userPrompt = "/nonexistent/file/path.md";
    config.useFile = true;
    engine->Run(config);

    // Should complete without throwing
    EXPECT_TRUE(true);
}
