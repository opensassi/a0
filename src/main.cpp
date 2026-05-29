#include "agent_core.h"
#include "component_registry.h"
#include "context_manager.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "invocation_logger.h"
#include "schema_inference_engine.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_tool_runner.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

static void loadEnvFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        setenv(key.c_str(), val.c_str(), 1);
    }
}

static bool hasFlag(int argc, char* argv[], const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) return true;
    }
    return false;
}

static std::string getFlag(int argc, char* argv[],
                            const std::string& name,
                            const std::string& defaultVal) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name && i + 1 < argc)
            return argv[i + 1];
    }
    const char* env = std::getenv(("A0_" + name.substr(2)).c_str());
    if (env) return env;
    return defaultVal;
}

int main(int argc, char* argv[]) {
    std::string envFilePath = ".env";
    std::string componentsDir = "./components";
    std::string apiKey;
    std::string mockUrl;
    std::string resumeSessionId;

    // first pass: find --env-file before env vars are needed
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--env-file" && i + 1 < argc) {
            envFilePath = argv[++i];
        }
    }
    loadEnvFile(envFilePath);

    // second pass: all other flags
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--env-file" && i + 1 < argc) {
            ++i; // already consumed
        } else if (arg == "--components-dir" && i + 1 < argc)
            componentsDir = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc)
            apiKey = argv[++i];
        else if (arg == "--mock-api" && i + 1 < argc)
            mockUrl = argv[++i];
        else if (arg == "--resume" && i + 1 < argc)
            resumeSessionId = argv[++i];
    }

    if (apiKey.empty()) {
        const char* envKey = std::getenv("DEEPSEEK_API_KEY");
        if (envKey) apiKey = envKey;
    }

    if (apiKey.empty()) {
        const char* home = std::getenv("HOME");
        if (home) {
            loadEnvFile(std::string(home) + "/.deepseek.env");
            const char* envKey = std::getenv("DEEPSEEK_API_KEY");
            if (envKey) apiKey = envKey;
        }
    }

    FileSystemComponentRegistry registry;
    SubprocessToolRunner toolRunner;
    DeepSeekProvider provider(apiKey);
    if (!mockUrl.empty())
        provider.setMockUrl(mockUrl);

    DefaultContextManager context;
    JsonLinesLogger logger;
    DefaultDependencyResolver depResolver(&registry);
    DefaultSchemaInferenceEngine inferenceEngine(&provider);

    // Docker initialization
    a0::docker::DockerContainerManager* containerMgr = nullptr;
    a0::docker::DockerComposeManager* composeMgr = nullptr;
    a0::docker::DockerToolRunnerImpl* dockerRunner = nullptr;

    bool noDocker = hasFlag(argc, argv, "--no-docker");
    if (!noDocker) {
        int idleTimeout = 300;
        int maxIdle = 10;
        std::string defaultImage = "ubuntu:22.04";

        std::string timeoutStr = getFlag(argc, argv, "--container-idle-timeout", "300");
        std::string maxIdleStr = getFlag(argc, argv, "--max-idle-containers", "10");
        defaultImage = getFlag(argc, argv, "--default-docker-image", "ubuntu:22.04");

        try { idleTimeout = std::stoi(timeoutStr); } catch (...) {}
        try { maxIdle = std::stoi(maxIdleStr); } catch (...) {}

        containerMgr = new a0::docker::DockerContainerManager(idleTimeout, maxIdle, defaultImage);
        composeMgr = new a0::docker::DockerComposeManager(idleTimeout);
        dockerRunner = new a0::docker::DockerToolRunnerImpl(containerMgr, composeMgr);
    }

    DefaultSkillRunner skillRunner(&toolRunner, &provider, &registry, &depResolver,
                                    dockerRunner, composeMgr);
    skillRunner.setComponentsDir(componentsDir);

    DefaultAgentCore core(&registry, &toolRunner, &skillRunner,
                          &provider, &context, &logger,
                          &depResolver, &inferenceEngine,
                          dockerRunner, composeMgr);

    if (!resumeSessionId.empty()) {
        core.resumeSession(resumeSessionId);
    }

    if (!core.init(componentsDir)) {
        std::cerr << "Failed to initialize components from: " << componentsDir << std::endl;
        return 1;
    }

    core.run();

    delete dockerRunner;
    delete composeMgr;
    delete containerMgr;
    return 0;
}
