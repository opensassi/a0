#include "docker/dependency_installer.h"
#include "docker/docker_cli_wrapper.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

static void setupMockPath() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* oldPath = std::getenv("PATH");
    std::string path = std::string(TEST_UNIT_DIR);
    if (oldPath) {
        path += ":" + std::string(oldPath);
    }
    setenv("PATH", path.c_str(), 1);
}

class DependencyInstallerTest : public ::testing::Test {
protected:
    void SetUp() override {
        setupMockPath();
    }
};

TEST_F(DependencyInstallerTest, InstallEmptyPackagesDoesNothing) {
    EXPECT_NO_THROW({
        a0::docker::DependencyInstaller::install("test_container", {});
    });
}

TEST_F(DependencyInstallerTest, InstallSinglePackage) {
    EXPECT_NO_THROW({
        a0::docker::DependencyInstaller::install("test_container",
                                                  {"curl"});
    });
}

TEST_F(DependencyInstallerTest, InstallMultiplePackages) {
    EXPECT_NO_THROW({
        a0::docker::DependencyInstaller::install("test_container",
                                                  {"curl", "git", "jq"});
    });
}

TEST_F(DependencyInstallerTest, InstallIdempotent) {
    EXPECT_NO_THROW({
        a0::docker::DependencyInstaller::install("test_container",
                                                  {"curl"});
        a0::docker::DependencyInstaller::install("test_container",
                                                  {"curl"});
    });
}

TEST_F(DependencyInstallerTest, InstallWithDifferentContainerIds) {
    EXPECT_NO_THROW({
        a0::docker::DependencyInstaller::install("container_a",
                                                  {"curl"});
        a0::docker::DependencyInstaller::install("container_b",
                                                  {"git"});
    });
}