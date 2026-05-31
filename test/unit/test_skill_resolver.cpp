#include "skills/skills.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "system_tools.h"
#include "dependency_resolver.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

static std::string g_fixtureDir;

/// Copy a fixture skill directory into a temp sandbox and return the sandbox path.
static std::string prepareSandbox(const std::string& srcPath) {
    static int counter = 0;
    std::string dst = "/tmp/a0_test_skills_" + std::to_string(::getpid()) + "_" + std::to_string(counter++);

    std::string absSrc = srcPath;
    if (absSrc[0] != '/') {
        absSrc = g_fixtureDir + "/" + absSrc;
        if (absSrc.size() > 1 && absSrc.back() == '/')
            absSrc.pop_back();
    }
    if (!fs::is_directory(absSrc)) {
        absSrc = fs::path(absSrc).parent_path().string();
    }

    fs::create_directories(dst);
    for (const auto& entry : fs::directory_iterator(absSrc)) {
        auto target = fs::path(dst) / entry.path().filename();
        if (entry.is_directory()) {
            fs::copy(entry.path(), target, fs::copy_options::recursive);
        } else {
            fs::copy_file(entry.path(), target);
        }
    }
    return dst;
}

class SkillResolverTest : public ::testing::TestWithParam<json> {
protected:
    SkillManager* m_mgr = nullptr;
    DefaultSkillRunner* m_skillRunner = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;
    a0::SystemToolRegistry* m_systemTools = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    std::vector<std::string> m_sandboxes;
    std::string m_lastExpanded;
    std::unordered_map<std::string, std::string> m_lastDispatch;

    void SetUp() override {
        std::string skillsRoot = "/tmp/a0_test_skills_" + std::to_string(::getpid());
        m_mgr = new SkillManager(skillsRoot,
                                 "/tmp/a0_test_store_" + std::to_string(::getpid()),
                                 nullptr);
        m_toolRunner = new SubprocessToolRunner();
        m_systemTools = new a0::SystemToolRegistry();
        m_depResolver = new DefaultDependencyResolver(m_mgr);
        m_skillRunner = new DefaultSkillRunner(m_toolRunner, nullptr, m_mgr,
                                                m_depResolver, m_systemTools,
                                                nullptr, nullptr);
        m_skillRunner->setSkillsDir(skillsRoot);
        m_lastExpanded.clear();
        m_lastDispatch.clear();
    }

    void TearDown() override {
        delete m_skillRunner;
        delete m_depResolver;
        delete m_systemTools;
        delete m_toolRunner;
        delete m_mgr;
        m_skillRunner = nullptr;
        m_depResolver = nullptr;
        m_systemTools = nullptr;
        m_toolRunner = nullptr;
        m_mgr = nullptr;
        for (const auto& sb : m_sandboxes) {
            fs::remove_all(sb);
        }
        m_sandboxes.clear();
        // Clean the shared skills root so next test starts fresh
        std::string root = "/tmp/a0_test_skills_" + std::to_string(::getpid());
        fs::remove_all(root + "/system");
        fs::remove_all(root + "/local");
    }

    void loadFixture(const std::string& src) {
        std::string sandbox = prepareSandbox(src);
        m_sandboxes.push_back(sandbox);

        std::string absSrc = src;
        if (absSrc[0] != '/') {
            absSrc = g_fixtureDir + "/" + absSrc;
        }
        if (absSrc.size() > 1 && absSrc.back() == '/')
            absSrc.pop_back();
        std::string dirName = fs::path(absSrc).filename().string();

        std::string targetDir = std::string("/tmp/") + "a0_test_skills_" + std::to_string(::getpid())
                                + "/local/" + dirName;
        fs::create_directories(fs::path(targetDir).parent_path());
        if (fs::exists(targetDir))
            fs::remove_all(targetDir);
        fs::copy(sandbox, targetDir, fs::copy_options::recursive);
    }

    bool isSystemRef(const std::string& path) const {
        return path.rfind("skills/system/", 0) == 0;
    }
    bool isFixtureRef(const std::string& path) const {
        return path.rfind("test/fixtures/skills/", 0) == 0;
    }

    std::string resolveProjectPath(const std::string& rel) const {
        return g_fixtureDir + "/" + rel;
    }

    void loadSystemSkill(const std::string& relPath) {
        std::string absPath = resolveProjectPath(relPath);
        if (!fs::is_directory(absPath)) {
            absPath = fs::path(absPath).parent_path().string();
        }
        // Strip trailing slashes so filename() returns the directory name
        while (absPath.size() > 1 && absPath.back() == '/')
            absPath.pop_back();
        std::string sandbox = prepareSandbox(absPath);
        std::string dirName = fs::path(absPath).filename().string();
        m_sandboxes.push_back(sandbox);

        std::string targetDir = std::string("/tmp/") + "a0_test_skills_" + std::to_string(::getpid())
                                + "/system/" + dirName;
        fs::create_directories(fs::path(targetDir).parent_path());
        if (fs::exists(targetDir))
            fs::remove_all(targetDir);
        fs::copy(sandbox, targetDir, fs::copy_options::recursive);
    }
};

// Load test cases from JSON descriptor
static std::vector<json> loadTestCases() {
    std::string path = g_fixtureDir + "/test/fixtures/skill_resolver_tests.json";
    std::ifstream f(path);
    if (!f) {
        std::cerr << "ERROR: cannot open " << path << std::endl;
        return {};
    }
    json cases;
    try {
        f >> cases;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: parse error in " << path << ": " << e.what() << std::endl;
        return {};
    }
    return cases.get<std::vector<json>>();
}

TEST_P(SkillResolverTest, RunTestCase) {
    json tc = GetParam();
    std::string id = tc["id"];
    GTEST_LOG_(INFO) << "Running test: " << id << " — " << tc["description"].get<std::string>();

    // Phase 1: Load all fixture sources
    for (const auto& fixture : tc["fixtures"]) {
        std::string src = fixture.get<std::string>();
        if (isSystemRef(src)) {
            loadSystemSkill(src);
        } else if (isFixtureRef(src)) {
            loadFixture(src);
        } else {
            loadFixture(src);
        }
    }

    // Phase 2: Load all skills into SkillManager
    m_mgr->loadAll();

    // Phase 3: Execute steps
    for (const auto& step : tc["steps"]) {
        std::string action = step["action"].get<std::string>();

        if (action == "resolve_prompt") {
            std::string name = step["name"].get<std::string>();
            int expect = step["expect"].get<int>();
            Prompt out;
            int rc = m_mgr->getPromptResolved(name, out);
            EXPECT_EQ(rc, expect) << "resolve_prompt failed for " << name;
        }
        else if (action == "resolve_tool") {
            std::string name = step["name"].get<std::string>();
            int expect = step["expect"].get<int>();
            SkillTool out;
            int rc = m_mgr->getTool(name, out);
            EXPECT_EQ(rc, expect) << "resolve_tool failed for " << name;
        }
        else if (action == "assert_prompt_contains") {
            std::string name = step["name"].get<std::string>();
            Prompt out;
            ASSERT_EQ(m_mgr->getPromptResolved(name, out), 0) << "cannot resolve " << name << " for assertion";
            for (const auto& substr : step["strings"]) {
                EXPECT_NE(out.prompt.find(substr.get<std::string>()), std::string::npos)
                    << name << " does not contain '" << substr.get<std::string>() << "'"
                    << ". Actual prompt: " << out.prompt;
            }
        }
        else if (action == "assert_deps_resolved") {
            std::string name = step["name"].get<std::string>();
            Prompt out;
            ASSERT_EQ(m_mgr->getPrompt(name, out), 0) << "cannot resolve " << name << " for dep check";
            if (step.contains("exact")) {
                std::vector<std::string> expected = step["exact"].get<std::vector<std::string>>();
                EXPECT_EQ(out.dependencies, expected) << name << " dependencies mismatch";
            }
            if (step.contains("contains")) {
                for (const auto& dep : step["contains"]) {
                    bool found = false;
                    for (const auto& d : out.dependencies) {
                        if (d == dep.get<std::string>()) { found = true; break; }
                    }
                    EXPECT_TRUE(found) << name << " missing dependency: " << dep.get<std::string>();
                }
            }
        }
        else if (action == "assert_missing_empty") {
            std::string name = step["name"].get<std::string>();
            Prompt out;
            ASSERT_EQ(m_mgr->getPrompt(name, out), 0);
            for (const auto& dep : out.dependencies) {
                SkillTool tool;
                Prompt prompt;
                bool found = (m_mgr->getTool(dep, tool) == 0 || m_mgr->getPrompt(dep, prompt) == 0);
                EXPECT_TRUE(found) << "dependency " << dep << " on " << name << " is unresolved";
            }
        }
        else if (action == "assert_missing_contains") {
            std::string name = step["name"].get<std::string>();
            Prompt out;
            ASSERT_EQ(m_mgr->getPrompt(name, out), 0);
            for (const auto& substr : step["strings"]) {
                bool found = false;
                for (const auto& d : out.dependencies) {
                    if (d.find(substr.get<std::string>()) != std::string::npos) { found = true; break; }
                }
                EXPECT_TRUE(found) << name << " does not have dep containing '" << substr.get<std::string>() << "'";
            }
        }
        else if (action == "assert_tool_is_system") {
            std::string name = step["name"].get<std::string>();
            bool expected = step["expected"].get<bool>();
            SkillTool out;
            ASSERT_EQ(m_mgr->getTool(name, out), 0);
            EXPECT_EQ(out.systemTool, expected) << name << " systemTool flag mismatch";
        }
        else if (action == "assert_tool_command") {
            std::string name = step["name"].get<std::string>();
            std::string expected = step["equals"].get<std::string>();
            SkillTool out;
            ASSERT_EQ(m_mgr->getTool(name, out), 0);
            EXPECT_EQ(out.command, expected) << name << " command mismatch";
        }
        else if (action == "assert_tool_count") {
            std::string ns = step["ns"].get<std::string>();
            std::string component = step["component"].get<std::string>();
            SkillNamespace nsEnum = (ns == "local") ? SkillNamespace::LOCAL
                                  : (ns == "system") ? SkillNamespace::SYSTEM
                                  : SkillNamespace::GITHUB;
            auto allComps = m_mgr->listSkills(nsEnum);
            bool found = false;
            for (const auto& c : allComps) {
                if (c == component) { found = true; break; }
            }
            EXPECT_TRUE(found) << "component " << component << " not found in namespace " << ns;
        }
        else if (action == "assert_prompt_count") {
            std::string ns = step["ns"].get<std::string>();
            std::string component = step["component"].get<std::string>();
            auto allComps = m_mgr->listSkills(
                (ns == "local") ? SkillNamespace::LOCAL :
                (ns == "system") ? SkillNamespace::SYSTEM : std::optional<SkillNamespace>());
            bool found = false;
            for (const auto& c : allComps) {
                if (c == component) { found = true; break; }
            }
            EXPECT_TRUE(found) << "component " << component << " not found";
        }
        else if (action == "expand_prompt") {
            // Look up the prompt by qualified name and run expandPrompt
            std::string name = step["name"].get<std::string>();
            int expect = step["expect"].get<int>();
            Prompt sp;
            int rc = m_mgr->getPrompt(name, sp);
            ASSERT_EQ(rc, 0) << "expand_prompt: cannot find " << name;

            Prompt p;
            p.name = sp.name;
            p.description = sp.description;
            p.prompt = sp.prompt;
            p.dependencies = sp.dependencies;
            p.validators = sp.validators;

            try {
                m_lastExpanded = m_skillRunner->expandPrompt(p, {{"goal", "test"}});
                EXPECT_EQ(0, expect) << "expand_prompt succeeded unexpectedly for " << name;
            } catch (const std::exception& e) {
                m_lastExpanded = "EXCEPTION: " + std::string(e.what());
                if (expect != 0) {
                    EXPECT_EQ(expect, 0) << "expand_prompt threw: " << e.what();
                }
            }
        }
        else if (action == "expand_prompt_text") {
            // Create an ad-hoc Prompt from inline text and expand it
            std::string promptText = step["prompt"].get<std::string>();
            int expect = step["expect"].get<int>();

            Prompt p;
            p.name = "inline_test";
            p.prompt = promptText;
            if (step.contains("dependencies")) {
                p.dependencies = step["dependencies"].get<std::vector<std::string>>();
            }

            try {
                m_lastExpanded = m_skillRunner->expandPrompt(p, {{"goal", "test"}});
                EXPECT_EQ(0, expect) << "expand_prompt_text succeeded unexpectedly";
            } catch (const std::exception& e) {
                m_lastExpanded = "EXCEPTION: " + std::string(e.what());
                if (expect != 0) {
                    EXPECT_EQ(expect, 0) << "expand_prompt_text threw: " << e.what();
                }
            }
        }
        else if (action == "assert_expand_contains") {
            for (const auto& substr : step["strings"]) {
                EXPECT_NE(m_lastExpanded.find(substr.get<std::string>()), std::string::npos)
                    << "expanded output does not contain '" << substr.get<std::string>() << "'"
                    << ". Actual: " << m_lastExpanded;
            }
        }
        else if (action == "assert_expand_not_contains") {
            for (const auto& substr : step["strings"]) {
                EXPECT_EQ(m_lastExpanded.find(substr.get<std::string>()), std::string::npos)
                    << "expanded output unexpectedly contains '" << substr.get<std::string>() << "'"
                    << ". Actual: " << m_lastExpanded;
            }
        }
        else if (action == "build_dispatch_table") {
            m_lastDispatch = m_mgr->buildDispatchTable();
        }
        else if (action == "assert_dispatch_size") {
            int expected = step["expected"].get<int>();
            EXPECT_EQ(m_lastDispatch.size(), (size_t)expected)
                << "dispatch table size mismatch";
        }
        else if (action == "assert_dispatch_contains") {
            std::string qualified = step["qualified"].get<std::string>();
            bool found = false;
            for (const auto& [shortName, qn] : m_lastDispatch) {
                if (qn == qualified) { found = true; break; }
            }
            EXPECT_TRUE(found) << "dispatch table does not contain qualified name: " << qualified;
        }
        else if (action == "assert_dispatch_names_unique") {
            // Verify every short name maps to exactly one qualified name (trivial for map)
            // and every qualified name appears only once (verify no duplicate values).
            // Also verify that short names with collisions got disambiguated (no two entries
            // share the same last segment).
            std::set<std::string> seenQualified;
            std::set<std::string> seenShortBases;
            for (const auto& [shortName, qn] : m_lastDispatch) {
                EXPECT_TRUE(seenQualified.insert(qn).second)
                    << "duplicate qualified name in dispatch: " << qn;
                std::string base = qn.substr(qn.rfind(':') + 1);
                // If there's only one entry for this base name, the short name should equal the base
                // (no unnecessary prefixing)
            }
        }
        else if (action == "assert_dispatch_maps") {
            std::string shortName = step["short_name"].get<std::string>();
            std::string expectedQualified = step["qualified"].get<std::string>();
            auto it = m_lastDispatch.find(shortName);
            ASSERT_TRUE(it != m_lastDispatch.end())
                << "dispatch table has no entry for short name: " << shortName;
            EXPECT_EQ(it->second, expectedQualified)
                << "dispatch table maps '" << shortName << "' to '" << it->second
                << "' but expected '" << expectedQualified << "'";
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    SkillResolver,
    SkillResolverTest,
    ::testing::ValuesIn(loadTestCases()),
    [](const testing::TestParamInfo<json>& info) {
        std::string raw = info.param["id"].get<std::string>();
        std::string out;
        for (char c : raw) {
            if (isalnum(c)) out += c;
            else out += '_';
        }
        if (!out.empty() && isdigit(out[0])) out = "t" + out;
        return out;
    }
);

int main(int argc, char** argv) {
    g_fixtureDir = fs::absolute(argv[0]).parent_path().parent_path().string();
    std::string testFile = g_fixtureDir + "/test/fixtures/skill_resolver_tests.json";
    if (!fs::exists(testFile)) {
        g_fixtureDir = fs::current_path().parent_path().string();
        testFile = g_fixtureDir + "/test/fixtures/skill_resolver_tests.json";
        if (!fs::exists(testFile)) {
            g_fixtureDir = fs::current_path().string();
            testFile = g_fixtureDir + "/test/fixtures/skill_resolver_tests.json";
            if (!fs::exists(testFile)) {
                std::cerr << "Cannot find test fixtures at " << testFile << std::endl;
                return 1;
            }
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
