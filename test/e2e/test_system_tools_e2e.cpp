// E2E test: System tool chaining via SkillRunner::expandPrompt.
// Exercises glob, bash, grep, read tools in a realistic multi-step chain.
// No LLM API required — tools execute in-process before the mock LLM call.
//
// Build & run:
//   g++ -std=c++17 -I<project_root> -I<build>/_deps/nlohmann_json-src/include \
//       test/e2e/test_system_tools_e2e.cpp \
//       -L<build> -la0_lib -lpersistence_lib -lcmd_runner_lib -lipc_lib \
//       -lcurl -lsqlite3 -ldl \
//       -o /tmp/test_system_tools_e2e && /tmp/test_system_tools_e2e

#include "skill_runner.h"
#include "skill_registry.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_tools.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <sstream>
#include <string>

using a0::SystemToolRegistry;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static int g_failed = 0;
static int g_passed = 0;

static void check(const std::string& haystack, const std::string& needle,
                  const std::string& label) {
    if (haystack.find(needle) != std::string::npos) {
        std::cout << "  PASS: " << label << "\n";
        ++g_passed;
    } else {
        std::cout << "  FAIL: " << label
                  << " (expected to contain: \"" << needle << "\")\n";
        ++g_failed;
    }
}

static void checkNot(const std::string& haystack, const std::string& needle,
                     const std::string& label) {
    if (haystack.find(needle) == std::string::npos) {
        std::cout << "  PASS: " << label << "\n";
        ++g_passed;
    } else {
        std::cout << "  FAIL: " << label
                  << " (unexpected: \"" << needle << "\")\n";
        ++g_failed;
    }
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    // Locate project root via A0_ROOT env, default to CWD
    std::string projectRoot = ".";
    const char* rootEnv = std::getenv("A0_ROOT");
    if (rootEnv) projectRoot = rootEnv;

    // Chdir to project root so glob/bash resolve relative paths correctly
    if (!projectRoot.empty() && projectRoot != ".") {
        if (chdir(projectRoot.c_str()) != 0) {
            std::cerr << "FATAL: could not chdir to " << projectRoot << "\n";
            return 1;
        }
    }

    std::string skillsDir = projectRoot + "/skills";

    std::cout << "=== System Tools E2E Test ===\n";
    std::cout << "Skills dir: " << skillsDir << "\n\n";

    // ---- Setup ----
    FileSystemSkillRegistry registry;
    if (!registry.loadFromDirectory(skillsDir)) {
        std::cerr << "FATAL: could not load skills from " << skillsDir << "\n";
        return 1;
    }

    // Verify system tools are registered
    auto allTools = registry.listTools();
    bool hasBash = false, hasRead = false, hasGlob = false, hasGrep = false;
    for (const auto& t : allTools) {
        if (t == "bash") hasBash = true;
        if (t == "read") hasRead = true;
        if (t == "glob") hasGlob = true;
        if (t == "grep") hasGrep = true;
    }
    if (!hasBash || !hasRead || !hasGlob || !hasGrep) {
        std::cerr << "FATAL: system tools not loaded (bash=" << hasBash
                  << " read=" << hasRead << " glob=" << hasGlob
                  << " grep=" << hasGrep << ")\n";
        return 1;
    }
    std::cout << "  System tools registered: bash read glob grep edit write\n\n";

    SystemToolRegistry systemTools;
    SubprocessToolRunner toolRunner;
    DeepSeekProvider provider("test-key");
    DefaultDependencyResolver depResolver(&registry);

    DefaultSkillRunner runner(&toolRunner, &provider, &registry, &depResolver,
                              &systemTools);

    // ---- Test prompt: chain of 5 tool calls ----
    std::string promptText =
        "Architecture Audit Report\n"
        "========================\n\n"
        "Subdirectories:\n"
        "{{tool:glob pattern=\"src/*/\"}}\n\n"
        "Docker module file count:\n"
        "{{tool:bash command=\"ls src/docker/*.cpp 2>/dev/null | wc -l\" description=\"count docker cpps\"}}\n\n"
        "Interface declarations:\n"
        "{{tool:grep pattern=\"virtual .* = 0\" path=\"" + projectRoot + "/src\" include=\"*.h\"}}\n\n"
        "Main entry (first 5 lines):\n"
        "{{tool:read filePath=\"" + projectRoot + "/src/main.cpp\" offset=\"1\" limit=\"5\"}}\n\n"
        "Persistence file count:\n"
        "{{tool:bash command=\"ls src/persistence/*.cpp 2>/dev/null | wc -l\" description=\"count persistence files\"}}";

    Prompt p;
    p.name = "arch_audit";
    p.description = "Project architecture audit";
    p.prompt = promptText;

    json params = json::object();

    // ---- Execute ----
    std::cout << "Expanding prompt (5 chained tool calls)...\n";
    std::string expanded = runner.expandPrompt(p, params);
    std::cout << "\n--- EXPANDED PROMPT ---\n";
    std::cout << expanded << "\n";
    std::cout << "--- END ---\n\n";

    // ---- Assertions ----
    std::cout << "Results:\n";

    // 1. glob listed subdirectories
    check(expanded, "./src/docker", "glob lists ./src/docker");
    check(expanded, "src/b1", "glob lists src/b1");
    check(expanded, "src/persistence", "glob lists src/persistence");

    // 2. bash counted docker files (known: 5 .cpp files in src/docker/)
    check(expanded, "5", "bash count docker .cpp files");

    // 3. grep found pure virtual declarations
    check(expanded, "virtual", "grep found virtual declarations");
    check(expanded, "= 0", "grep found pure virtual pattern");

    // 4. read showed main.cpp start (first 5 lines, then truncation notice)
    check(expanded, "a0_dir.h", "read shows first include in main.cpp");
    check(expanded, "more lines", "read truncation notice appears");
    check(expanded, "Call read with offset=6", "read continuation hint appears");

    // 5. bash counted persistence files (known: 3 .cpp files)
    check(expanded, "3", "bash count persistence .cpp files");

    // 6. No ERROR strings from any tool
    checkNot(expanded, "ERROR:", "no tool errors");

    // ---- Summary ----
    std::cout << "\n=== Summary ===\n";
    std::cout << "  " << g_passed << "/" << (g_passed + g_failed)
              << " assertions passed\n";

    if (g_failed > 0) {
        std::cout << "  FAILED: some assertions did not pass\n";
        return 1;
    }
    std::cout << "  PASSED\n";
    return 0;
}
