#include "agent_core.h"
#include "component_registry.h"
#include "context_manager.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "invocation_logger.h"
#include "schema_inference_engine.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

class AgentCoreTest : public ::testing::Test {
protected:
    FileSystemComponentRegistry registry;
    SubprocessToolRunner toolRunner;
    DeepSeekProvider* provider;
    DefaultContextManager context;
    JsonLinesLogger* logger;
    DefaultDependencyResolver* depResolver;
    DefaultSchemaInferenceEngine* inferenceEngine;
    DefaultSkillRunner* skillRunner;
    DefaultAgentCore* core;

    std::string m_testComponents = "test_agent_components";
    std::string m_testLogs = "test_agent_logs";

    void SetUp() override {
        std::string cmd = "rm -rf " + m_testComponents + " " + m_testLogs
            + " && mkdir -p " + m_testComponents + " " + m_testLogs;
        system(cmd.c_str());
        {
            std::ofstream f(m_testComponents + "/bash.tool.json");
            f << "{\"name\":\"bash\",\"description\":\"bash\",\"command\":\"bash\",\"inputMode\":\"stdin\"}";
        }

        provider = new DeepSeekProvider("test-key");
        provider->setMockUrl("http://localhost:18081/v1/chat/completions");
        logger = new JsonLinesLogger(m_testLogs);
        depResolver = new DefaultDependencyResolver(&registry);
        inferenceEngine = new DefaultSchemaInferenceEngine(provider);
        skillRunner = new DefaultSkillRunner(&toolRunner, provider, &registry);
        core = new DefaultAgentCore(&registry, &toolRunner, skillRunner,
                                     provider, &context, logger,
                                     depResolver, inferenceEngine);
    }

    void TearDown() override {
        delete core;
        delete skillRunner;
        delete inferenceEngine;
        delete depResolver;
        delete logger;
        delete provider;
        std::string cmd = "rm -rf " + m_testComponents + " " + m_testLogs;
        system(cmd.c_str());
    }
};

TEST_F(AgentCoreTest, InitReturnsTrue) {
    EXPECT_TRUE(core->init(m_testComponents));
}

TEST_F(AgentCoreTest, InitCreatesSessionId) {
    ASSERT_TRUE(core->init(m_testComponents));
    EXPECT_FALSE(core->currentSessionId().empty());
}

TEST_F(AgentCoreTest, InitWithInvalidDirReturnsFalse) {
    EXPECT_FALSE(core->init("/nonexistent/path"));
}

TEST_F(AgentCoreTest, ProcessGoalEmpty) {
    ASSERT_TRUE(core->init(m_testComponents));
    json result = core->processGoal("");
    ASSERT_TRUE(result.is_string());
    std::string s = result.get<std::string>();
    EXPECT_FALSE(s.empty());
}

TEST_F(AgentCoreTest, ProcessGoalLogsEntry) {
    ASSERT_TRUE(core->init(m_testComponents));
    core->processGoal("do something");
    auto sessions = logger->listSessions();
    EXPECT_FALSE(sessions.empty());
}

TEST_F(AgentCoreTest, ResumeNonexistentSession) {
    ASSERT_TRUE(core->init(m_testComponents));
    EXPECT_FALSE(core->resumeSession("nonexistent"));
}

TEST_F(AgentCoreTest, InitThenResume) {
    ASSERT_TRUE(core->init(m_testComponents));
    std::string sid = core->currentSessionId();
    // do some work so the session has log entries
    core->processGoal("bash");
    DefaultAgentCore core2(&registry, &toolRunner, skillRunner,
                            provider, &context, logger,
                            depResolver, inferenceEngine);
    EXPECT_TRUE(core2.init(m_testComponents));
    bool resumed = core2.resumeSession(sid);
    EXPECT_TRUE(resumed);
}

TEST_F(AgentCoreTest, ProcessGoalMultipleCalls) {
    ASSERT_TRUE(core->init(m_testComponents));
    for (int i = 0; i < 3; ++i) {
        json r = core->processGoal("goal " + std::to_string(i));
        EXPECT_TRUE(r.is_string());
    }
}

TEST_F(AgentCoreTest, ProcessGoalBeforeInitThrows) {
    EXPECT_THROW(core->processGoal("test"), std::logic_error);
}
