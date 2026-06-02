#include "skill_runner.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_handlers.h"
#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

#ifndef TEST_PROJECT_DIR
#define TEST_PROJECT_DIR "."
#endif

class SystemToolsE2ETest : public ::testing::Test {
protected:
    SkillManager* m_mgr = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;
    DeepSeekProvider* m_provider = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    DefaultSkillRunner* m_runner = nullptr;
    std::string m_projectRoot;

    void SetUp() override {
        m_projectRoot = TEST_PROJECT_DIR;
        std::string skillsDir = m_projectRoot + "/skills";
        std::string pid = std::to_string(::getpid());

        m_mgr = new SkillManager(
            skillsDir,
            "/tmp/a0_test_e2e_store_" + pid,
            nullptr
        );
        ASSERT_EQ(m_mgr->loadAll(), 0) << "Failed to load skills from " << skillsDir;

        SkillTool tool;
        ASSERT_EQ(m_mgr->getTool("system:bash:bash", tool), 0);
        ASSERT_EQ(m_mgr->getTool("system:fs:read", tool), 0);
        ASSERT_EQ(m_mgr->getTool("system:fs:glob", tool), 0);
        ASSERT_EQ(m_mgr->getTool("system:fs:grep", tool), 0);

        // Register C++ handlers on SkillManager
        m_mgr->registerHandler("system:bash:bash", [](const json& p) { return a0::xBash(p); });
        m_mgr->registerHandler("system:fs:read", [](const json& p) { return a0::xRead(p); });
        m_mgr->registerHandler("system:fs:glob", [](const json& p) { return a0::xGlob(p); });
        m_mgr->registerHandler("system:fs:grep", [](const json& p) { return a0::xGrep(p); });
        m_mgr->registerHandler("system:fs:edit", [](const json& p) { return a0::xEdit(p); });
        m_mgr->registerHandler("system:fs:write", [](const json& p) { return a0::xWrite(p); });

        m_toolRunner = new SubprocessToolRunner();
        m_provider = new DeepSeekProvider("test-key");
        m_depResolver = new DefaultDependencyResolver(m_mgr);

        m_runner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver
        );

        fs::current_path(m_projectRoot);
    }

    void TearDown() override {
        delete m_runner;
        delete m_depResolver;
        delete m_provider;
        delete m_toolRunner;
        delete m_mgr;
        std::string pid = std::to_string(::getpid());
        fs::remove_all("/tmp/a0_test_e2e_store_" + pid);
    }
};

TEST_F(SystemToolsE2ETest, SystemToolChainExpandsCorrectly) {
    std::string promptText =
        "Architecture Audit Report\n"
        "========================\n\n"
        "Subdirectories:\n"
        "{{tool:system:fs:glob pattern=\"src/*/\"}}\n\n"
        "Docker module file count:\n"
        "{{tool:system:bash command=\"ls src/docker/*.cpp 2>/dev/null | wc -l\" description=\"count docker cpps\"}}\n\n"
        "Interface declarations:\n"
        "{{tool:system:fs:grep pattern=\"virtual .* = 0\" path=\"" + m_projectRoot + "/src\" include=\"*.h\"}}\n\n"
        "Main entry (first 5 lines):\n";

    Prompt p;
    p.name = "test";
    p.prompt = promptText;
    p.ns = "system";
    p.component = "fs";

    std::string expanded = m_runner->expandPrompt(p, {});
    std::cerr << "DEBUG expanded len=" << expanded.size() << " text=[" << expanded.substr(0, 300) << "]" << std::endl;
    std::cerr << "DEBUG has_glob=" << (expanded.find("src/b1/") != std::string::npos)
              << " has_bash=" << (expanded.find("count") != std::string::npos)
              << " has_virtual=" << (expanded.find("virtual") != std::string::npos) << std::endl;
    std::cerr << "DEBUG contains_tool=" << (expanded.find("{{tool:") != std::string::npos) << std::endl;
    EXPECT_TRUE(expanded.find("src/b1") != std::string::npos ||
                expanded.find("src/c2") != std::string::npos)
        << "Expected expanded output to contain subdirectory names like src/b1, src/c2. Got: " << expanded.substr(0, 200);
    EXPECT_TRUE(expanded.find("5") != std::string::npos)
        << "Expected bash tool output to contain '5'. Got: " << expanded.substr(0, 200);
    EXPECT_TRUE(expanded.find("virtual") != std::string::npos)
        << "Expected grep output to contain 'virtual' declarations. Got: " << expanded.substr(0, 200);
}
