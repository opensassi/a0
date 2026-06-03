#include "skills/skill_loader.h"
#include "skills/skills.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace a0::skills;

TEST(SkillSchemaTest, ValidateAllSkillFiles) {
    // Find the project's skills directory relative to the test binary location,
    // or use the TEST_PROJECT_DIR compile-time define
    std::string skillsDir;
#ifdef TEST_PROJECT_DIR
    skillsDir = std::string(TEST_PROJECT_DIR) + "/skills";
#else
    skillsDir = fs::current_path().string() + "/skills";
    if (!fs::is_directory(skillsDir)) {
        skillsDir = fs::current_path().string() + "/../skills";
    }
#endif

    // Collect all skill.json files
    std::vector<std::string> skillFiles;
    for (auto& p : fs::recursive_directory_iterator(skillsDir)) {
        if (p.path().filename() == "skill.json") {
            skillFiles.push_back(p.path().string());
        }
    }

    ASSERT_FALSE(skillFiles.empty()) << "No skill.json files found in " << skillsDir;

    // Load the schema once
    std::string schemaPath = skillsDir + "/schema.json";
    ASSERT_TRUE(fs::exists(schemaPath)) << "Schema not found: " << schemaPath;

    std::ifstream ifs(schemaPath);
    ASSERT_TRUE(ifs.is_open());
    nlohmann::json schemaJson;
    ASSERT_NO_THROW(ifs >> schemaJson);

    valijson::Schema schema;
    valijson::SchemaParser parser;
    valijson::adapters::NlohmannJsonAdapter schemaAdapter(schemaJson);
    ASSERT_NO_THROW(parser.populateSchema(schemaAdapter, schema));

    valijson::Validator validator;

    int fileIndex = 0;
    for (const auto& skillFile : skillFiles) {
        std::ifstream sf(skillFile);
        ASSERT_TRUE(sf.is_open()) << "Cannot open: " << skillFile;
        nlohmann::json manifestJson;
        ASSERT_NO_THROW(sf >> manifestJson) << "Parse error: " << skillFile;

        valijson::adapters::NlohmannJsonAdapter targetAdapter(manifestJson);
        valijson::ValidationResults results;
        bool valid = validator.validate(schema, targetAdapter, &results);

        if (!valid) {
            std::string errorStr;
            for (const auto& error : results) {
                std::string path;
                for (const auto& seg : error.context) {
                    path += "/" + seg;
                }
                errorStr += "  " + path + ": " + error.description + "\n";
            }
            FAIL() << "Schema validation failed for " << skillFile << ":\n" << errorStr;
        } else {
            std::cout << "  [" << (++fileIndex) << "] OK: " << skillFile << std::endl;
        }
    }
}
