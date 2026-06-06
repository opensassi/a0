#include "agent_core.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_handlers.h"
#include "context_manager.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

class ExternalA0Test : public ::testing::Test {
protected:
    std::string m_skillsDir;
    std::string m_storeDir;
    std::string m_a0Dir;

    SkillManager* m_mgr = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;
    DeepSeekProvider* m_provider = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    DefaultSkillRunner* m_skillRunner = nullptr;
    DefaultContextManager* m_context = nullptr;
    DefaultAgentCore* m_core = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        m_a0Dir = "/tmp/a0_ext_test_a0_" + pid;
        m_skillsDir = "/tmp/a0_ext_test_skills_" + pid;
        m_storeDir = "/tmp/a0_ext_test_store_" + pid;
        fs::remove_all(m_a0Dir);
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::create_directories(m_skillsDir + "/system");
        fs::create_directories(m_skillsDir + "/local");

        m_mgr = new SkillManager(m_skillsDir, m_storeDir, nullptr);
        m_toolRunner = new SubprocessToolRunner();
        m_provider = new DeepSeekProvider("test-key");
        m_depResolver = new DefaultDependencyResolver(m_mgr);
        m_context = new DefaultContextManager();

        m_mgr->setToolRunner(m_toolRunner);
        m_mgr->registerHandler("system_git_*", [](const json& p, const a0::skills::HandlerContext& ctx) {
            return a0::xGitCommand(ctx.subcommand, p);
        });

        m_skillRunner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver);
        m_skillRunner->setSkillsDir(m_skillsDir);
        m_provider->setMockUrl("http://127.0.0.1:18999/v1/chat/completions");

        m_core = new DefaultAgentCore(
            m_toolRunner, m_skillRunner, m_provider, m_context,
            m_depResolver, m_mgr, nullptr, nullptr, nullptr);
    }

    void TearDown() override {
        delete m_core;
        delete m_skillRunner;
        delete m_context;
        delete m_depResolver;
        delete m_provider;
        delete m_toolRunner;
        delete m_mgr;
        fs::remove_all(m_a0Dir);
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
    }
};

TEST_F(ExternalA0Test, InitWithoutA0DirSkipsClone) {
    m_core->setExternalRepo("https://github.com/opensassi/a0");
    bool ok = m_core->init(m_skillsDir, "");
    EXPECT_TRUE(ok);
}

TEST_F(ExternalA0Test, InitWithoutExternalRepoSkipsClone) {
    m_core->setExternalRepo("");
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    EXPECT_TRUE(ok);
}

TEST_F(ExternalA0Test, InitWithExternalRepoDoesNotCrash) {
    m_core->setExternalRepo("https://github.com/opensassi/a0");
    // May fail to clone if no network, but must not crash
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    EXPECT_TRUE(ok);
}

TEST_F(ExternalA0Test, InitExistingExternalDirUpdates) {
    // Create a fake external/a0 directory to simulate existing clone
    fs::create_directories(m_a0Dir + "/external/a0");
    std::ofstream f(m_a0Dir + "/external/a0/README.md");
    f << "fake" << std::endl;

    m_core->setExternalRepo("https://github.com/opensassi/a0");
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    EXPECT_TRUE(ok);
    // Directory still exists
    EXPECT_TRUE(fs::is_directory(m_a0Dir + "/external/a0"));
}
