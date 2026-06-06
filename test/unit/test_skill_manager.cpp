#include "skills/skills.h"
#include "tool_runner.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static void writeManifest(const std::string& root, const std::string& ns, const std::string& component, const json& data) {
    fs::create_directories(root + "/" + ns + "/" + component);
    std::ofstream f(root + "/" + ns + "/" + component + "/skill.json");
    f << data.dump(2);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SkillManagerTest : public ::testing::Test {
protected:
    std::string m_skillsRoot;
    std::string m_storeRoot;
    SkillManager* m_mgr = nullptr;

    void SetUp() override {
        auto pid = std::to_string(::getpid());
        m_skillsRoot = "/tmp/a0_test_sm_skills_" + pid;
        m_storeRoot = "/tmp/a0_test_sm_store_" + pid;
        fs::create_directories(m_skillsRoot + "/system");
        fs::create_directories(m_skillsRoot + "/local");
        m_mgr = new SkillManager(m_skillsRoot, m_storeRoot, nullptr);
    }

    void TearDown() override {
        delete m_mgr;
        fs::remove_all(m_skillsRoot);
        fs::remove_all(m_storeRoot);
    }
};

// ===========================================================================
// parseQualifiedName / buildQualifiedName
// ===========================================================================

TEST(SkillManagerHelperTest, ParseQualifiedName_Full) {
    std::string ns, component, name;
    EXPECT_TRUE(parseQualifiedName("github_alice_utils_list_files", ns, component, name));
    EXPECT_EQ(ns, "github_alice");
    EXPECT_EQ(component, "utils");
    EXPECT_EQ(name, "list_files");
}

TEST(SkillManagerHelperTest, ParseQualifiedName_NoName) {
    std::string ns, component, name;
    EXPECT_TRUE(parseQualifiedName("local_meta", ns, component, name));
    EXPECT_EQ(ns, "local");
    EXPECT_EQ(component, "meta");
    EXPECT_EQ(name, "meta");
}

TEST(SkillManagerHelperTest, ParseQualifiedName_Invalid) {
    std::string ns, component, name;
    EXPECT_FALSE(parseQualifiedName("justaname", ns, component, name));
}

TEST(SkillManagerHelperTest, BuildQualifiedName_NameEqualsComponent) {
    EXPECT_EQ(buildQualifiedName("local", "my_comp", "my_comp"), "local_my_comp");
}

TEST(SkillManagerHelperTest, BuildQualifiedName_NameDifferent) {
    EXPECT_EQ(buildQualifiedName("local", "my_comp", "my_tool"), "local_my_comp_my_tool");
}

// ===========================================================================
// getPromptResolved
// ===========================================================================

TEST_F(SkillManagerTest, GetPromptResolved_InvalidName) {
    Prompt out;
    EXPECT_EQ(m_mgr->getPromptResolved("invalid", out), -2);
}

TEST_F(SkillManagerTest, GetPromptResolved_NotFound) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    Prompt out;
    EXPECT_EQ(m_mgr->getPromptResolved("local_test-comp_nonexistent", out), -2);
}

TEST_F(SkillManagerTest, GetPromptResolved_SimpleChain) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "base"}, {"description", ""}, {"prompt", "BASE_TEXT"}},
            json{{"name", "child"}, {"description", ""}, {"chain", json::array({"base"})}, {"prompt", "CHILD_TEXT"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    Prompt out;
    ASSERT_EQ(m_mgr->getPromptResolved("local_test-comp_child", out), 0);
    EXPECT_EQ(out.prompt, "BASE_TEXT\n\nCHILD_TEXT");
}

TEST_F(SkillManagerTest, GetPromptResolved_DeepChain) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "level1"}, {"description", ""}, {"prompt", "LVL1"}},
            json{{"name", "level2"}, {"description", ""}, {"chain", json::array({"level1"})}, {"prompt", "LVL2"}},
            json{{"name", "level3"}, {"description", ""}, {"chain", json::array({"level2"})}, {"prompt", "LVL3"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    Prompt out;
    ASSERT_EQ(m_mgr->getPromptResolved("local_test-comp_level3", out), 0);
    EXPECT_EQ(out.prompt, "LVL1\n\nLVL2\n\nLVL3");
}

TEST_F(SkillManagerTest, GetPromptResolved_CrossComponentChain) {
    writeManifest(m_skillsRoot, "local", "comp-a", json{
        {"name", "comp_a"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "greeting"}, {"description", ""}, {"prompt", "Hello"}}
        })}
    });
    writeManifest(m_skillsRoot, "local", "comp-b", json{
        {"name", "comp_b"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "polite"}, {"description", ""}, {"chain", json::array({"local_comp-a_greeting"})}, {"prompt", "World"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    Prompt out;
    ASSERT_EQ(m_mgr->getPromptResolved("local_comp-b_polite", out), 0);
    EXPECT_EQ(out.prompt, "Hello\n\nWorld");
}

TEST_F(SkillManagerTest, GetPromptResolved_MissingChainEntry) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "child"}, {"description", ""}, {"chain", json::array({"nonexistent"})}, {"prompt", "TEXT"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    Prompt out;
    ASSERT_EQ(m_mgr->getPromptResolved("local_test-comp_child", out), 0);
    EXPECT_EQ(out.prompt, "TEXT");
}

TEST_F(SkillManagerTest, GetPromptResolved_PreservesMetadata) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "base"}, {"description", "base desc"}, {"prompt", "BASE"}},
            json{{"name", "child"}, {"description", "child desc"}, {"chain", json::array({"base"})}, {"prompt", "CHILD"}, {"dependencies", json::array({"dep1"})}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    Prompt out;
    ASSERT_EQ(m_mgr->getPromptResolved("local_test-comp_child", out), 0);
    EXPECT_EQ(out.name, "child");
    EXPECT_EQ(out.description, "child desc");
    ASSERT_EQ(out.dependencies.size(), 1);
    EXPECT_EQ(out.dependencies[0], "dep1");
    EXPECT_EQ(out.ns, "local");
    EXPECT_EQ(out.component, "test-comp");
}

// ===========================================================================
// resolveName
// ===========================================================================

TEST_F(SkillManagerTest, ResolveName_AlreadyQualified) {
    std::string out;
    EXPECT_EQ(m_mgr->resolveName("local", "comp", "system_read", out), 0);
    EXPECT_EQ(out, "system_read");
}

TEST_F(SkillManagerTest, ResolveName_WithinComponent_Found) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "my_tool"}, {"description", ""}, {"command", "echo hi"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    std::string out;
    EXPECT_EQ(m_mgr->resolveName("local", "test-comp", "my_tool", out), 0);
    EXPECT_EQ(out, "local_test-comp_my_tool");
}

TEST_F(SkillManagerTest, ResolveName_WithinComponent_FoundPrompt) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "my_prompt"}, {"description", ""}, {"prompt", "hello"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    std::string out;
    EXPECT_EQ(m_mgr->resolveName("local", "test-comp", "my_prompt", out), 0);
    EXPECT_EQ(out, "local_test-comp_my_prompt");
}

TEST_F(SkillManagerTest, ResolveName_WithinComponent_NotFound) {
    std::string out;
    EXPECT_EQ(m_mgr->resolveName("local", "test-comp", "nonexistent", out), 1);
    EXPECT_EQ(out, "nonexistent");
}

// ===========================================================================
// addTool / addPrompt
// ===========================================================================

TEST_F(SkillManagerTest, AddAndGetTool) {
    SkillTool t;
    t.name = "my_tool";
    t.description = "test tool";
    t.command = "echo hello";
    t.inputMode = "args";
    EXPECT_EQ(m_mgr->addTool("test-comp", t), 0);

    SkillTool got;
    EXPECT_EQ(m_mgr->getTool("local_test-comp_my_tool", got), 0);
    EXPECT_EQ(got.name, "my_tool");
    EXPECT_EQ(got.command, "echo hello");
}

TEST_F(SkillManagerTest, AddAndGetPrompt) {
    Prompt p;
    p.name = "my_prompt";
    p.description = "test prompt";
    p.prompt = "Hello world";
    EXPECT_EQ(m_mgr->addPrompt("test-comp", p), 0);

    Prompt got;
    EXPECT_EQ(m_mgr->getPrompt("local_test-comp_my_prompt", got), 0);
    EXPECT_EQ(got.name, "my_prompt");
    EXPECT_EQ(got.prompt, "Hello world");
}

TEST_F(SkillManagerTest, AddTool_PersistsThroughLoadAll) {
    SkillTool t;
    t.name = "persist_tool";
    t.description = "persists";
    t.command = "echo persist";
    t.inputMode = "args";
    ASSERT_EQ(m_mgr->addTool("persist-comp", t), 0);

    ASSERT_EQ(m_mgr->loadAll(), 0);

    SkillTool got;
    EXPECT_EQ(m_mgr->getTool("local_persist-comp_persist_tool", got), 0);
    EXPECT_EQ(got.name, "persist_tool");
}

TEST_F(SkillManagerTest, AddPrompt_Twice) {
    Prompt p;
    p.name = "p1";
    p.description = "";
    p.prompt = "first";
    ASSERT_EQ(m_mgr->addPrompt("multi-comp", p), 0);

    Prompt p2;
    p2.name = "p2";
    p2.description = "";
    p2.prompt = "second";
    ASSERT_EQ(m_mgr->addPrompt("multi-comp", p2), 0);

    Prompt got;
    EXPECT_EQ(m_mgr->getPrompt("local_multi-comp_p1", got), 0);
    EXPECT_EQ(got.prompt, "first");
    EXPECT_EQ(m_mgr->getPrompt("local_multi-comp_p2", got), 0);
    EXPECT_EQ(got.prompt, "second");
}

// ===========================================================================
// getTool — error paths
// ===========================================================================

TEST_F(SkillManagerTest, GetTool_InvalidName) {
    SkillTool t;
    EXPECT_EQ(m_mgr->getTool("invalid", t), -2);
}

TEST_F(SkillManagerTest, GetTool_NonexistentComponent) {
    SkillTool t;
    EXPECT_EQ(m_mgr->getTool("local_nope_tool", t), -1);
}

TEST_F(SkillManagerTest, GetTool_NonexistentToolName) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "exists"}, {"description", ""}, {"command", "echo"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    SkillTool t;
    EXPECT_EQ(m_mgr->getTool("local_test-comp_missing", t), -2);
}

// ===========================================================================
// updateTool
// ===========================================================================

TEST_F(SkillManagerTest, UpdateTool) {
    SkillTool t;
    t.name = "my_tool";
    t.description = "original";
    t.command = "echo original";
    t.inputMode = "args";
    ASSERT_EQ(m_mgr->addTool("test-comp", t), 0);

    SkillTool upd;
    upd.name = "my_tool";
    upd.description = "updated";
    upd.command = "echo updated";
    upd.inputMode = "args";
    EXPECT_EQ(m_mgr->updateTool("test-comp", "my_tool", upd), 0);

    SkillTool got;
    ASSERT_EQ(m_mgr->getTool("local_test-comp_my_tool", got), 0);
    EXPECT_EQ(got.description, "updated");
    EXPECT_EQ(got.command, "echo updated");
}

TEST_F(SkillManagerTest, UpdateTool_Nonexistent) {
    SkillTool t;
    t.name = "new_tool";
    t.description = "";
    t.command = "echo";
    t.inputMode = "args";
    EXPECT_EQ(m_mgr->updateTool("nope", "new_tool", t), -1);
}

// ===========================================================================
// install (stubs)
// ===========================================================================

TEST_F(SkillManagerTest, Install_NoCommit) {
    EXPECT_EQ(m_mgr->install("https://example.com/repo.git"), 0);
}

TEST_F(SkillManagerTest, Install_WithCommit) {
    EXPECT_EQ(m_mgr->install("https://example.com/repo.git", "abc123"), 0);
}

TEST_F(SkillManagerTest, Install_ForceFlag) {
    EXPECT_EQ(m_mgr->install("https://example.com/repo.git", true), 0);
}

// ===========================================================================
// remove
// ===========================================================================

TEST_F(SkillManagerTest, Remove_LocalComponent) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "my_tool"}, {"description", ""}, {"command", "echo"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    EXPECT_TRUE(fs::exists(m_skillsRoot + "/local/test-comp"));

    EXPECT_EQ(m_mgr->remove("local_test-comp"), 0);

    SkillTool t;
    EXPECT_EQ(m_mgr->getTool("local_test-comp_my_tool", t), -1);
}

TEST_F(SkillManagerTest, Remove_SystemComponent_Error) {
    writeManifest(m_skillsRoot, "system", "bash", json{
        {"name", "bash"},
        {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    EXPECT_EQ(m_mgr->remove("system_bash"), -1);
}

TEST_F(SkillManagerTest, Remove_InvalidName) {
    EXPECT_EQ(m_mgr->remove("invalid"), -1);
}

TEST_F(SkillManagerTest, Remove_UnknownNamespace) {
    EXPECT_EQ(m_mgr->remove("unknown:comp"), -1);
}

// ===========================================================================
// gc
// ===========================================================================

TEST_F(SkillManagerTest, Gc_DryRun) {
    EXPECT_EQ(m_mgr->gc(true), 0);
}

TEST_F(SkillManagerTest, Gc_Real) {
    EXPECT_EQ(m_mgr->gc(false), 0);
}

// ===========================================================================
// validate
// ===========================================================================

TEST_F(SkillManagerTest, Validate_InvalidName) {
    std::string report;
    EXPECT_EQ(m_mgr->validate("invalid", "", report), -1);
    EXPECT_NE(report.find("invalid qualified name"), std::string::npos);
}

TEST_F(SkillManagerTest, Validate_UnknownNamespace) {
    std::string report;
    EXPECT_EQ(m_mgr->validate("unknown_comp", "", report), -1);
    EXPECT_EQ(report, "unknown namespace: unknown");
}

TEST_F(SkillManagerTest, Validate_ValidComponent_NoLogs) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "my_tool"}, {"description", ""}, {"command", "echo"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    std::string report;
    int rc = m_mgr->validate("local_test-comp_my_tool", "abc123", report);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(report, "no historical logs — validation skipped");
}

// ===========================================================================
// buildDispatchTable
// ===========================================================================

TEST_F(SkillManagerTest, BuildDispatchTable_Empty) {
    auto table = m_mgr->buildDispatchTable();
    EXPECT_TRUE(table.empty());
}

TEST_F(SkillManagerTest, BuildDispatchTable_NoCollisions) {
    writeManifest(m_skillsRoot, "local", "test-comp", json{
        {"name", "test_comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "parser"}, {"description", ""}, {"command", "echo"}, {"inputMode", "args"}}
        })},
        {"prompts", json::array({
            json{{"name", "greet"}, {"description", ""}, {"prompt", "Hi"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto table = m_mgr->buildDispatchTable();
    EXPECT_EQ(table.size(), 2);
    EXPECT_EQ(table["parser"], "local_test-comp_parser");
    EXPECT_EQ(table["greet"], "local_test-comp_greet");
}

TEST_F(SkillManagerTest, BuildDispatchTable_WithCollisions) {
    writeManifest(m_skillsRoot, "local", "comp-a", json{
        {"name", "comp_a"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "parser"}, {"description", ""}, {"command", "echo a"}, {"inputMode", "args"}}
        })}
    });
    writeManifest(m_skillsRoot, "local", "comp-b", json{
        {"name", "comp_b"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "parser"}, {"description", ""}, {"command", "echo b"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto table = m_mgr->buildDispatchTable();
    EXPECT_EQ(table.size(), 2);
    // Both qualified names must be present; iteration order of the loader's
    // internal unordered_map determines which component gets the bare short name.
    bool aOk = (table["parser"] == "local_comp-a_parser" || table["comp-a_parser"] == "local_comp-a_parser");
    bool bOk = (table["parser"] == "local_comp-b_parser" || table["comp-b_parser"] == "local_comp-b_parser");
    EXPECT_TRUE(aOk) << "comp_a:parser not found in dispatch table";
    EXPECT_TRUE(bOk) << "comp_b:parser not found in dispatch table";
    // The bare short name must map to exactly one of them
    EXPECT_TRUE(table.find("parser") != table.end());
    auto val = table["parser"];
    EXPECT_TRUE(val == "local_comp-a_parser" || val == "local_comp-b_parser");
    // The disambiguated name must map to the other one
    std::string other = (val == "local_comp-a_parser") ? "local_comp-b_parser" : "local_comp-a_parser";
    std::string disambiguated = (other == "local_comp-a_parser") ? "comp-a_parser" : "comp-b_parser";
    EXPECT_EQ(table[disambiguated], other);
}

TEST_F(SkillManagerTest, BuildDispatchTable_NamespaceCollision) {
    writeManifest(m_skillsRoot, "system", "shared", json{
        {"name", "shared"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "util"}, {"description", ""}, {"command", "echo sys"}, {"inputMode", "args"}}
        })}
    });
    writeManifest(m_skillsRoot, "local", "shared", json{
        {"name", "shared"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "util"}, {"description", ""}, {"command", "echo local"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto table = m_mgr->buildDispatchTable();
    EXPECT_EQ(table.size(), 2);
    // SYSTEM is iterated first, so "util" maps to the system one
    EXPECT_EQ(table["util"], "system_shared_util");
    EXPECT_EQ(table["shared_util"], "local_shared_util");
}

// ===========================================================================
// listSkills
// ===========================================================================

TEST_F(SkillManagerTest, ListSkills_AllNamespaces) {
    writeManifest(m_skillsRoot, "system", "bash", json{
        {"name", "bash"},
        {"version", "1.0.0"}
    });
    writeManifest(m_skillsRoot, "local", "my-comp", json{
        {"name", "my_comp"},
        {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto all = m_mgr->listSkills(std::nullopt);
    EXPECT_EQ(all.size(), 2);
}

TEST_F(SkillManagerTest, ListSkills_FilteredNamespace) {
    writeManifest(m_skillsRoot, "system", "bash", json{
        {"name", "bash"},
        {"version", "1.0.0"}
    });
    writeManifest(m_skillsRoot, "local", "my-comp", json{
        {"name", "my_comp"},
        {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);

    auto systemOnly = m_mgr->listSkills(SkillNamespace::SYSTEM);
    EXPECT_EQ(systemOnly.size(), 1);
    EXPECT_EQ(systemOnly[0], "bash");

    auto localOnly = m_mgr->listSkills(SkillNamespace::LOCAL);
    EXPECT_EQ(localOnly.size(), 1);
    EXPECT_EQ(localOnly[0], "my-comp");
}

TEST_F(SkillManagerTest, ListSkills_Empty) {
    auto all = m_mgr->listSkills(std::nullopt);
    EXPECT_TRUE(all.empty());
}

// ===========================================================================
// executeToolStreaming
// ===========================================================================

TEST_F(SkillManagerTest, ExecuteToolStreaming_SystemHandlerSyncFallback) {
    m_mgr->registerHandler("system_test_read", [](const json& p, const HandlerContext&) {
        return ::a0::HandlerResult{p.value("file", "ok"), {}};
    });

    std::string output;
    auto handle = m_mgr->executeToolStreaming("system_test_read",
        {{"file", "content"}},
        [&](const std::string& data, const std::string&) { output += data; });

    EXPECT_EQ(output, "content");
    // No streaming handle returned for sync fallback
    EXPECT_EQ(handle.streamId, 0);
}

TEST_F(SkillManagerTest, ExecuteToolStreaming_WildcardSyncFallback) {
    m_mgr->registerHandler("system_git_*", [](const json& p, const HandlerContext& ctx) {
        return ::a0::HandlerResult{"ran: " + ctx.subcommand, {}};
    });

    std::string output;
    auto handle = m_mgr->executeToolStreaming("system_git_status",
        json::object(),
        [&](const std::string& data, const std::string&) { output += data; });

    EXPECT_EQ(output, "ran: status");
    EXPECT_EQ(handle.streamId, 0);
}

TEST_F(SkillManagerTest, ExecuteToolStreaming_UnknownTool) {
    std::string output;
    auto handle = m_mgr->executeToolStreaming("local_nonexistent_ghost",
        json::object(),
        [&](const std::string& data, const std::string&) { output += data; });

    EXPECT_TRUE(output.find("ERROR: tool not found") != std::string::npos);
    EXPECT_EQ(handle.streamId, 0);
}

TEST_F(SkillManagerTest, ExecuteToolStreaming_HandlerTakesPrecedenceOverCommandTool) {
    // When both a handler and a manifest tool exist, the handler wins.
    // This verifies the step-1 dispatch in executeToolStreaming.
    writeManifest(m_skillsRoot, "local", "stream-comp", json{
        {"name", "stream_comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "echo_stream"}, {"description", "streaming echo"},
                 {"command", "echo"}, {"inputMode", "stdin"},
                 {"streaming", true}, {"timeoutSecs", 5}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);

    // Register a handler that shields the command tool
    m_mgr->registerHandler("local_stream-comp_echo_stream", [](const json& p, const HandlerContext&) {
        return ::a0::HandlerResult{p.value("input", "default"), {}};
    });

    std::string output;
    auto handle = m_mgr->executeToolStreaming("local_stream-comp_echo_stream",
        {{"input", "hello handler"}},
        [&](const std::string& data, const std::string&) { output += data; });

    // Handler path produces output synchronously via callback
    EXPECT_EQ(output, "hello handler");
    EXPECT_EQ(handle.streamId, 0);
}

TEST_F(SkillManagerTest, ExecuteToolWithMeta_ExactHandler) {
    m_mgr->registerHandler("system_test_exec", [](const json& p, const HandlerContext&) {
        return ::a0::HandlerResult{"executed: " + p.value("val", ""), {}};
    });
    auto hr = m_mgr->executeToolWithMeta("system_test_exec", {{"val", "hello"}});
    EXPECT_EQ(hr.output, "executed: hello");
}

TEST_F(SkillManagerTest, ExecuteToolWithMeta_WildcardHandler) {
    m_mgr->registerHandler("system_test_*", [](const json& p, const HandlerContext& ctx) {
        return ::a0::HandlerResult{"wildcard: " + ctx.subcommand, {}};
    });
    auto hr = m_mgr->executeToolWithMeta("system_test_cmd", {{}});
    EXPECT_EQ(hr.output, "wildcard: cmd");
}

TEST_F(SkillManagerTest, ExecuteToolWithMeta_TwoPartAlias) {
    m_mgr->registerHandler("system_bash_bash", [](const json& p, const HandlerContext&) {
        return ::a0::HandlerResult{"bash output", {}};
    });
    auto hr = m_mgr->executeToolWithMeta("system_bash", {{}});
    EXPECT_EQ(hr.output, "bash output");
}

TEST_F(SkillManagerTest, ExecuteToolWithMeta_NotFound) {
    auto hr = m_mgr->executeToolWithMeta("local_nonexistent_tool", {{}});
    EXPECT_TRUE(hr.output.find("ERROR: tool not found") != std::string::npos);
}

TEST_F(SkillManagerTest, Schemas_DefaultOnly) {
    writeManifest(m_skillsRoot, "local", "comp", json{
        {"name", "comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "visible"}, {"command", "echo"}, {"inputMode", "args"}, {"default", true},
                 {"parameters", {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}}}},
            json{{"name", "hidden"}, {"command", "echo"}, {"inputMode", "args"},
                 {"parameters", {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}}}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto schemas = m_mgr->schemas(true);
    bool hasVisible = false;
    bool hasHidden = false;
    for (const auto& s : schemas) {
        if (s.name == "visible") hasVisible = true;
        if (s.name == "hidden") hasHidden = true;
    }
    EXPECT_TRUE(hasVisible);
    EXPECT_FALSE(hasHidden);
}

TEST_F(SkillManagerTest, Schemas_All) {
    writeManifest(m_skillsRoot, "local", "comp", json{
        {"name", "comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "a"}, {"command", "echo"}, {"inputMode", "args"},
                 {"parameters", {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}}}},
            json{{"name", "b"}, {"command", "echo"}, {"inputMode", "args"},
                 {"parameters", {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}}}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto schemas = m_mgr->schemas(false);
    EXPECT_EQ(schemas.size(), 2);
}

TEST_F(SkillManagerTest, RegisterHandlerAndExecute) {
    bool called = false;
    m_mgr->registerHandler("system_test_handler", [&](const json&, const HandlerContext&) {
        called = true;
        return ::a0::HandlerResult{"ok", {}};
    });
    m_mgr->executeTool("system_test_handler", {});
    EXPECT_TRUE(called);
}

TEST_F(SkillManagerTest, ExecuteTool_CommandToolWithRunner) {
    writeManifest(m_skillsRoot, "local", "comp", json{
        {"name", "comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "echo_tool"}, {"command", "echo hello"}, {"inputMode", "stdin"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    SubprocessToolRunner runner;
    m_mgr->setToolRunner(&runner);
    json result = m_mgr->executeTool("local_comp_echo_tool", {{"input", ""}});
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "hello\n");
}

TEST_F(SkillManagerTest, GetManifest_Existing) {
    writeManifest(m_skillsRoot, "local", "comp", json{
        {"name", "comp"},
        {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    a0::skills::SkillManifest m;
    EXPECT_EQ(m_mgr->getManifest(a0::skills::SkillNamespace::LOCAL, "comp", m), 0);
    EXPECT_EQ(m.name, "comp");
}

TEST_F(SkillManagerTest, GetManifest_NotFound) {
    a0::skills::SkillManifest m;
    EXPECT_EQ(m_mgr->getManifest(a0::skills::SkillNamespace::LOCAL, "nonexistent", m), -1);
}

TEST_F(SkillManagerTest, MissingHandlers_AllRegistered) {
    auto missing = m_mgr->missingHandlers();
    EXPECT_TRUE(missing.empty());
}

TEST_F(SkillManagerTest, MissingHandlers_Unregistered) {
    writeManifest(m_skillsRoot, "local", "comp", json{
        {"name", "comp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "sys"}, {"command", ""}, {"systemTool", true}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto missing = m_mgr->missingHandlers();
    EXPECT_FALSE(missing.empty());
}

TEST_F(SkillManagerTest, GetTool_TwoPartAlias) {
    writeManifest(m_skillsRoot, "local", "mycomp", json{
        {"name", "mycomp"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "mycomp"}, {"command", "echo"}, {"inputMode", "args"}}
        })}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    SkillTool t;
    // Two-part name: local-mycomp → ns=local, component=mycomp, name=mycomp
    EXPECT_EQ(m_mgr->getTool("local_mycomp", t), 0);
}

TEST_F(SkillManagerTest, ListSkills_SystemOnly) {
    writeManifest(m_skillsRoot, "system", "bash", json{
        {"name", "bash"}, {"version", "1.0.0"}
    });
    writeManifest(m_skillsRoot, "local", "mycomp", json{
        {"name", "mycomp"}, {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto sys = m_mgr->listSkills(a0::skills::SkillNamespace::SYSTEM);
    EXPECT_EQ(sys.size(), 1);
    EXPECT_EQ(sys[0], "bash");
}

TEST_F(SkillManagerTest, ListSkills_LocalOnly) {
    writeManifest(m_skillsRoot, "local", "mycomp", json{
        {"name", "mycomp"}, {"version", "1.0.0"}
    });
    ASSERT_EQ(m_mgr->loadAll(), 0);
    auto local = m_mgr->listSkills(a0::skills::SkillNamespace::LOCAL);
    EXPECT_EQ(local.size(), 1);
}

TEST_F(SkillManagerTest, ToolStateAccessor) {
    ToolState& ts = m_mgr->toolState();
    ts.set("k", json("v"));
    EXPECT_EQ(ts.get("k"), json("v"));
}

TEST_F(SkillManagerTest, DockerRunnerSetNull) {
    m_mgr->setDockerRunner(nullptr);
    SUCCEED();
}

TEST_F(SkillManagerTest, DockerSecurityFilterSetNull) {
    m_mgr->setDockerSecurityFilter(nullptr);
    SUCCEED();
}
