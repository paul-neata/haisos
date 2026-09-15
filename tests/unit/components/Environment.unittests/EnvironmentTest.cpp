#include <gtest/gtest.h>
#include <algorithm>
#include "Environment.h"

using namespace Haisos;

namespace {

std::vector<std::string> Sorted(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    return names;
}

TEST(EnvironmentTest, VariablesAddSetRemoveAndEnumerate) {
    auto environment = CreateEnvironment();

    EXPECT_TRUE(environment->AddVariable("A", "1"));
    // Add refuses to overwrite; Set does it happily.
    EXPECT_FALSE(environment->AddVariable("A", "2"));
    EXPECT_EQ(environment->GetVariable("A").value_or(""), "1");
    environment->SetVariable("A", "2");
    EXPECT_EQ(environment->GetVariable("A").value_or(""), "2");

    environment->SetVariable("B", "3");
    EXPECT_EQ(Sorted(environment->GetVariableNames()), (std::vector<std::string>{"A", "B"}));

    EXPECT_TRUE(environment->HasVariable("B"));
    EXPECT_TRUE(environment->RemoveVariable("B"));
    EXPECT_FALSE(environment->RemoveVariable("B"));
    EXPECT_FALSE(environment->HasVariable("B"));
    EXPECT_FALSE(environment->GetVariable("B").has_value());
}

TEST(EnvironmentTest, SecretsCanBeNamedButNotRead) {
    auto environment = CreateEnvironment();

    EXPECT_TRUE(environment->AddSecret("api", "s3cret"));
    EXPECT_FALSE(environment->AddSecret("api", "other"));
    EXPECT_TRUE(environment->HasSecret("api"));
    EXPECT_EQ(environment->GetSecretNames(), (std::vector<std::string>{"api"}));

    environment->SetSecret("api", "rotated");
    EXPECT_TRUE(environment->RemoveSecret("api"));
    EXPECT_FALSE(environment->RemoveSecret("api"));
    EXPECT_FALSE(environment->HasSecret("api"));
}

TEST(EnvironmentTest, SecretsCopyBetweenEnvironmentsWithoutBeingRevealed) {
    auto source = CreateEnvironment();
    source->SetSecret("api", "s3cret");
    auto destination = CreateEnvironment();

    EXPECT_TRUE(destination->AddSecretFrom("llm_token", *source, "api"));
    EXPECT_TRUE(destination->HasSecret("llm_token"));
    // The name is the destination's own choice, not the source's.
    EXPECT_FALSE(destination->HasSecret("api"));

    // Adding again fails on the taken name; setting replaces it.
    EXPECT_FALSE(destination->AddSecretFrom("llm_token", *source, "api"));
    EXPECT_TRUE(destination->SetSecretFrom("llm_token", *source, "api"));

    // A secret the source does not have cannot be copied.
    EXPECT_FALSE(destination->AddSecretFrom("missing", *source, "nope"));
    EXPECT_FALSE(destination->SetSecretFrom("missing", *source, "nope"));
    EXPECT_FALSE(destination->HasSecret("missing"));
}

TEST(EnvironmentTest, LLMIdentifiersRoundTrip) {
    auto environment = CreateEnvironment();

    LLMIdentifier identifier;
    identifier.endpoint = "http://localhost:11434/api/chat";
    identifier.modelName = "llama3";
    identifier.secretName = "api";

    EXPECT_TRUE(environment->AddLLMIdentifier("default", identifier));
    EXPECT_FALSE(environment->AddLLMIdentifier("default", identifier));
    EXPECT_TRUE(environment->HasLLMIdentifier("default"));

    auto stored = environment->GetLLMIdentifier("default");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->endpoint, identifier.endpoint);
    EXPECT_EQ(stored->modelName, "llama3");
    EXPECT_EQ(stored->secretName.value_or(""), "api");
    EXPECT_FALSE(stored->apiToken.has_value());

    LLMIdentifier replacement = identifier;
    replacement.modelName = "kimi-k2.6:cloud";
    environment->SetLLMIdentifier("default", replacement);
    EXPECT_EQ(environment->GetLLMIdentifier("default")->modelName, "kimi-k2.6:cloud");

    EXPECT_EQ(environment->GetLLMIdentifierNames(), (std::vector<std::string>{"default"}));
    EXPECT_TRUE(environment->RemoveLLMIdentifier("default"));
    EXPECT_FALSE(environment->GetLLMIdentifier("default").has_value());
}

TEST(EnvironmentTest, CloneCopiesEverythingAndThenDiverges) {
    auto original = CreateEnvironment();
    original->SetVariable("A", "1");
    original->SetSecret("api", "s3cret");
    LLMIdentifier identifier;
    identifier.endpoint = "http://localhost:11434/api/chat";
    identifier.modelName = "llama3";
    identifier.apiToken = "token";
    original->SetLLMIdentifier("default", identifier);

    auto clone = original->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->GetVariable("A").value_or(""), "1");
    EXPECT_TRUE(clone->HasSecret("api"));
    ASSERT_TRUE(clone->GetLLMIdentifier("default").has_value());
    EXPECT_EQ(clone->GetLLMIdentifier("default")->apiToken.value_or(""), "token");

    // The clone carried the secret across, so it can pass it on in turn.
    auto third = CreateEnvironment();
    EXPECT_TRUE(third->AddSecretFrom("api", *clone, "api"));

    clone->SetVariable("A", "2");
    clone->RemoveSecret("api");
    EXPECT_EQ(original->GetVariable("A").value_or(""), "1");
    EXPECT_TRUE(original->HasSecret("api"));
}

} // namespace
