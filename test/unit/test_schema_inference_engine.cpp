#include "schema_inference_engine.h"
#include <gtest/gtest.h>

class SchemaInferenceTest : public ::testing::Test {
protected:
    class MockProvider : public InferenceProvider {
    public:
        std::string response;
        int callCount = 0;
        std::string secondResponse; // returned on second call, for retry tests

        std::string complete(const std::string& sys, const std::string& usr) override {
            (void)sys;
            (void)usr;
            callCount++;
            if (callCount == 2 && !secondResponse.empty())
                return secondResponse;
            return response;
        }
        CompletionResponse complete(
            const std::string&,
            const std::vector<Message>&,
            const std::vector<ToolSchema>&) override
        { return {response, {}}; }
        void setMockUrl(const std::string& url) override { (void)url; }
    };

    MockProvider* provider;
    DefaultSchemaInferenceEngine* engine;

    void SetUp() override {
        provider = new MockProvider();
        provider->response = R"({
            "name": "inferred_tool",
            "description": "tool inferred from description",
            "command": "echo hello",
            "inputMode": "stdin"
        })";
        engine = new DefaultSchemaInferenceEngine(provider);
    }

    void TearDown() override {
        delete engine;
        delete provider;
    }
};

TEST_F(SchemaInferenceTest, InferToolReturnsTool) {
    Tool t = engine->inferTool("a tool that says hello");
    EXPECT_EQ(t.name, "inferred_tool");
    EXPECT_EQ(t.command, "echo hello");
}

TEST_F(SchemaInferenceTest, InferToolNameMatchesResponse) {
    Tool t = engine->inferTool("count lines in file");
    EXPECT_EQ(t.name, "inferred_tool");
    EXPECT_FALSE(t.description.empty());
    EXPECT_FALSE(t.command.empty());
}

TEST_F(SchemaInferenceTest, InferPromptReturnsPrompt) {
    provider->response = R"({
        "name": "file_finder",
        "description": "finds files",
        "prompt": "search for files",
        "dependencies": ["bash"],
        "validators": []
    })";
    Prompt p = engine->inferPrompt("find files matching a pattern");
    EXPECT_EQ(p.name, "file_finder");
    EXPECT_EQ(p.prompt, "search for files");
}

TEST_F(SchemaInferenceTest, InferSkillDependenciesParsed) {
    provider->response = R"({
        "name": "processor",
        "description": "processes data",
        "prompt": "process it",
        "dependencies": ["bash", "grep"],
        "validators": [{"toolName": "extract_json"}]
    })";
    Prompt p2 = engine->inferPrompt("process some data");
    ASSERT_EQ(p2.dependencies.size(), 2u);
    EXPECT_EQ(p2.dependencies[0], "bash");
    EXPECT_EQ(p2.dependencies[1], "grep");
}

TEST_F(SchemaInferenceTest, InvalidJsonResponse) {
    provider->response = "not json at all";
    EXPECT_THROW(engine->inferTool("something"), std::exception);
}

TEST_F(SchemaInferenceTest, EmptyDescription) {
    provider->response = R"({"name":"","description":"invalid","command":"","inputMode":"stdin"})";
    EXPECT_NO_THROW(engine->inferTool(""));
}

TEST_F(SchemaInferenceTest, ShortDescription) {
    provider->response = R"({"name":"one","description":"a","command":"ls","inputMode":"stdin"})";
    Tool t = engine->inferTool("ls");
    EXPECT_EQ(t.name, "one");
}

TEST_F(SchemaInferenceTest, InferToolEmptyResponseThrows) {
    provider->response = "";
    EXPECT_THROW(engine->inferTool("anything"), std::runtime_error);
}

TEST_F(SchemaInferenceTest, InferPromptEmptyResponseThrows) {
    provider->response = "";
    EXPECT_THROW(engine->inferPrompt("anything"), std::runtime_error);
}

TEST_F(SchemaInferenceTest, InferPromptRetryOnInvalidJson) {
    // First call returns invalid JSON; retry returns valid
    provider->response = "not json";
    provider->secondResponse = R"({"name":"retry_tool","description":"retry","prompt":"retry prompt","dependencies":[],"validators":[]})";
    provider->callCount = 0;
    Prompt p = engine->inferPrompt("retry");
    EXPECT_EQ(p.name, "retry_tool");
}

TEST_F(SchemaInferenceTest, InferToolRetryOnInvalidJson) {
    provider->response = "invalid{json";
    provider->secondResponse = R"({"name":"retry_tool","description":"inferred retry","command":"echo retry","inputMode":"stdin"})";
    provider->callCount = 0;
    Tool t = engine->inferTool("retry");
    EXPECT_EQ(t.name, "retry_tool");
}
