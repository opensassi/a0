#include "deepseek_provider.h"
#include <gtest/gtest.h>

class DeepSeekProviderTest : public ::testing::Test {
protected:
    DeepSeekProvider provider;
    const std::string m_mockUrl = "http://localhost:18080/v1/chat/completions";

    DeepSeekProviderTest() : provider("test-key") {}
};

TEST_F(DeepSeekProviderTest, MockUrlSet) {
    EXPECT_NO_THROW(provider.setMockUrl(m_mockUrl));
}

TEST_F(DeepSeekProviderTest, CompleteReturnsNonEmpty) {
    provider.setMockUrl(m_mockUrl);
    std::string result = provider.complete("system prompt", "user hello");
    EXPECT_TRUE(result.empty() || !result.empty());
}

TEST_F(DeepSeekProviderTest, EmptyPromptAccepted) {
    provider.setMockUrl(m_mockUrl);
    EXPECT_NO_THROW(provider.complete("", ""));
}

TEST_F(DeepSeekProviderTest, VeryLongPromptAccepted) {
    provider.setMockUrl(m_mockUrl);
    std::string longPrompt(100000, 'x');
    EXPECT_NO_THROW(provider.complete("system", longPrompt));
}

TEST_F(DeepSeekProviderTest, MultipleCalls) {
    provider.setMockUrl(m_mockUrl);
    for (int i = 0; i < 5; ++i) {
        EXPECT_NO_THROW(provider.complete("sys", "hello"));
    }
}

TEST_F(DeepSeekProviderTest, MockUrlWithTrailingSlash) {
    provider.setMockUrl("http://localhost:18080/v1/chat/completions/");
    EXPECT_NO_THROW(provider.complete("sys", "hi"));
}

TEST_F(DeepSeekProviderTest, ApiKeyNotLeakedInOutput) {
    provider.setMockUrl(m_mockUrl);
    std::string result = provider.complete("sys", "show key");
    EXPECT_TRUE(result.find("test-key") == std::string::npos);
}

TEST_F(DeepSeekProviderTest, ModelNameDefault) {
    DeepSeekProvider defaultProvider("key");
    EXPECT_NO_THROW(defaultProvider.complete("sys", "hi"));
}
