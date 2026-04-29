#include <gtest/gtest.h>
#include "ToolFactory.h"

using namespace Haisos;

TEST(ToolFactoryTest, CreateToolWithName) {
    ToolFactory factory;
    auto tool = factory.CreateTool("get_current_date_time");
    EXPECT_NE(tool, nullptr);
}

TEST(ToolFactoryTest, CreateUnknownToolReturnsNullptr) {
    ToolFactory factory;
    auto tool = factory.CreateTool("unknown_tool_xyz");
    EXPECT_EQ(tool, nullptr);
}

TEST(ToolFactoryTest, GetCurrentDateTimeToolReturnsValidFormat) {
    ToolFactory factory;
    auto tool = factory.CreateTool("get_current_date_time");
    ASSERT_NE(tool, nullptr);

    ToolResult result = tool->Call(nullptr, {});

    // Should return non-empty content
    EXPECT_FALSE(result.content.empty());
    EXPECT_FALSE(result.isError);

    std::string content = result.content;

    // Should contain space separator (date and time)
    EXPECT_NE(content.find(' '), std::string::npos);

    // Format: YYYY-MM-DD HH:MM:SS
    // Check length (19 chars for format "2024-01-15 12:30:45")
    EXPECT_EQ(content.length(), 19);
}

TEST(ToolFactoryTest, GetAvailableTools) {
    ToolFactory factory;
    auto tools = factory.GetAvailableTools();
    EXPECT_FALSE(tools.empty());

    bool found = false;
    for (const auto& name : tools) {
        if (name == "get_current_date_time") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ToolFactoryTest, GetAvailableToolDescriptionsIncludeSchemas) {
    ToolFactory factory;
    auto descriptions = factory.GetAvailableToolDescriptions();
    EXPECT_FALSE(descriptions.empty());

    bool found = false;
    for (const auto& desc : descriptions) {
        if (std::get<0>(desc) == "get_current_date_time") {
            found = true;
            EXPECT_FALSE(std::get<1>(desc).empty());
            EXPECT_TRUE(std::get<2>(desc).is_object());
            EXPECT_EQ(std::get<2>(desc).value("type", ""), "object");
        }
    }
    EXPECT_TRUE(found);
}
