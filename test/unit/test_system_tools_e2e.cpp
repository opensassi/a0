#include "skill_runner.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_tools.h"
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
    a0::SystemToolRegistry* m_systemTools = nullptr;
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
            "/tmp/a0_test_e2e_logs_" + pid
        );
        ASSERT_EQ(m_mgr->loadAll(), 0) << "Failed to load skills from " << skillsDir;

        SkillTool tool;
        ASSERT_EQ(m_mgr->getTool("system:bash", tool), 0);
        ASSERT_EQ(m_mgr->getTool("system:read", tool), 0);
        ASSERT_EQ(m_mgr->getTool("system:glob", tool), 0);
        ASSERT_EQ(m_mgr->getTool("system:grep", tool), 0);

        m_systemTools = new a0::SystemToolRegistry();
        m_toolRunner = new SubprocessToolRunner();
        m_provider = new DeepSeekProvider("test-key");
        m_depResolver = new DefaultDependencyResolver(m_mgr);

        m_runner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver, m_systemTools
        );

        fs::current_path(m_projectRoot);
    }

    void TearDown() override {
        delete m_runner;
        delete m_depResolver;
        delete m_provider;
        delete m_toolRunner;
        delete m_systemTools;
        delete m_mgr;
        std::string pid = std::to_string(::getpid());
        fs::remove_all("/tmp/a0_test_e2e_store_" + pid);
        fs::remove_all("/tmp/a0_test_e2e_logs_" + pid);
    }
};

TEST_F(SystemToolsE2ETest, SystemToolChainExpandsCorrectly) {
    std::string promptText =
        "Architecture Audit Report\n"
        "========================\n\n"
        "Subdirectories:\n"
        "{{tool:system:glob pattern=\"src/*/\"}}\n\n"
        "Docker module file count:\n"
        "{{tool:system:bash command=\"ls src/docker/*.cpp 2>/dev/null | wc -l\" description=\"count docker cpps\"}}\n\n"
        "Interface declarations:\n"
        "{{tool:system:grep pattern=\"virtual .* = 0\" path=\"" + m_projectRoot + "/src\" include=\"*.h\"}}\n\n"
        "Main entry (first 5 lines):\n"
        "{{tool:system:read filePath=\"" + m_projectRoot + "/src/main.cpp\" offset=\"1\" limit=\"5\"}}\n\n"
        "Persistence file count:\n"
        "{{tool:system:bash command=\"ls src/persistence/*.cpp 2>/dev/null | wc -l\" description=\"count persistence files\"}}";

    Prompt p;
    p.name = "arch_audit";
    p.description = "Project architecture audit";
    p.prompt = promptText;

    json params = json::object();

    std::string expanded = m_runner->expandPrompt(p, params);

    // glob listed subdirectories
    EXPECT_NE(expanded.find("./src/docker"), std::string::npos);
    EXPECT_NE(expanded.find("src/b1"), std::string::npos);
    EXPECT_NE(expanded.find("src/persistence"), std::string::npos);

    // bash counted docker files (5 .cpp files in src/docker/)
    EXPECT_NE(expanded.find("5"), std::string::npos);

    // grep found pure virtual declarations
    EXPECT_NE(expanded.find("virtual"), std::string::npos);
    EXPECT_NE(expanded.find("= 0"), std::string::npos);

    // read showed main.cpp start
    EXPECT_NE(expanded.find("a0_dir.h"), std::string::npos);
    EXPECT_NE(expanded.find("more lines"), std::string::npos);
    EXPECT_NE(expanded.find("Call read with offset=6"), std::string::npos);

    // bash counted persistence files (3 .cpp files)
    EXPECT_NE(expanded.find("3"), std::string::npos);

    // No ERROR strings from any tool
    EXPECT_EQ(expanded.find("ERROR:"), std::string::npos);
}
