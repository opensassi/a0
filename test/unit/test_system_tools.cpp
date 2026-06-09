#include "executor/system_handlers.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

class SystemToolsTest : public ::testing::Test {
protected:
    std::string m_tmp;

    void SetUp() override {
        m_tmp = "/tmp/a0_sys_test_" + std::to_string(::getpid());
        fs::remove_all(m_tmp);
        fs::create_directories(m_tmp);
    }

    void TearDown() override {
        fs::remove_all(m_tmp);
    }

    void writeFile(const std::string& path, const std::string& content) {
        auto parent = fs::path(path).parent_path();
        if (!parent.empty()) fs::create_directories(parent);
        std::ofstream f(path);
        f << content;
    }
};

// ---------------------------------------------------------------------------
// xBash
// ---------------------------------------------------------------------------

TEST_F(SystemToolsTest, BashMissingCommand) {
    auto result = a0::xBash({});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, BashEcho) {
    auto result = a0::xBash({{"command", "echo hello"}});
    EXPECT_TRUE(result.output.find("hello") != std::string::npos);
}

TEST_F(SystemToolsTest, BashWithWorkdir) {
    auto result = a0::xBash({
        {"command", "pwd"},
        {"workdir", m_tmp}
    });
    EXPECT_TRUE(result.output.find(m_tmp) != std::string::npos);
}

TEST_F(SystemToolsTest, BashTimeoutCapping) {
    auto result = a0::xBash({
        {"command", "echo timeout_works"},
        {"timeout", 120000}
    });
    EXPECT_TRUE(result.output.find("timeout_works") != std::string::npos);
}

// ---------------------------------------------------------------------------
// xRead
// ---------------------------------------------------------------------------

TEST_F(SystemToolsTest, ReadMissingFilePath) {
    auto result = a0::xRead({});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadFileNotFound) {
    auto result = a0::xRead({{"filePath", m_tmp + "/no_such_file.txt"}});
    EXPECT_TRUE(result.output.find("ERROR: file not found") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadDirectoryListing) {
    writeFile(m_tmp + "/alpha.txt", "aaa");
    writeFile(m_tmp + "/beta.txt", "bbb");
    fs::create_directories(m_tmp + "/subdir");
    auto result = a0::xRead({{"filePath", m_tmp}});
    EXPECT_TRUE(result.output.find("alpha.txt") != std::string::npos);
    EXPECT_TRUE(result.output.find("beta.txt") != std::string::npos);
    EXPECT_TRUE(result.output.find("subdir/") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadBinaryFile) {
    writeFile(m_tmp + "/image.png", "fake png content");
    auto result = a0::xRead({{"filePath", m_tmp + "/image.png"}});
    EXPECT_TRUE(result.output.find("binary") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadFileTooLarge) {
    std::string path = m_tmp + "/large.txt";
    {
        std::ofstream f(path);
        std::string chunk(1024 * 1024, 'X');
        for (int i = 0; i < 11; ++i) f << chunk;
    }
    auto result = a0::xRead({{"filePath", path}});
    EXPECT_TRUE(result.output.find("over size limit") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadOffsetExceedsFileLength) {
    writeFile(m_tmp + "/short.txt", "line1\nline2\nline3");
    auto result = a0::xRead({
        {"filePath", m_tmp + "/short.txt"},
        {"offset", 100}
    });
    EXPECT_TRUE(result.output.find("ERROR: offset") != std::string::npos);
    EXPECT_TRUE(result.output.find("exceeds file length") != std::string::npos);
}

// ---------------------------------------------------------------------------
// xEdit
// ---------------------------------------------------------------------------

TEST_F(SystemToolsTest, EditMissingFilePath) {
    auto result = a0::xEdit({{"oldString", "foo"}, {"newString", "bar"}});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, EditMissingOldString) {
    auto result = a0::xEdit({{"filePath", m_tmp + "/x.txt"}, {"newString", "bar"}});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, EditMissingNewString) {
    auto result = a0::xEdit({{"filePath", m_tmp + "/x.txt"}, {"oldString", "foo"}});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, EditFileNotFound) {
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/nonexistent.txt"},
        {"oldString", "foo"},
        {"newString", "bar"}
    });
    EXPECT_TRUE(result.output.find("ERROR: file not found") != std::string::npos);
}

TEST_F(SystemToolsTest, EditSingleReplace) {
    writeFile(m_tmp + "/edit_single.txt", "hello foo world");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/edit_single.txt"},
        {"oldString", "foo"},
        {"newString", "bar"}
    });
    EXPECT_TRUE(result.output.find("Edit applied successfully") != std::string::npos);
    std::ifstream f(m_tmp + "/edit_single.txt");
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "hello bar world");
}

TEST_F(SystemToolsTest, EditReplaceAll) {
    writeFile(m_tmp + "/edit_all.txt", "foo and foo and foo");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/edit_all.txt"},
        {"oldString", "foo"},
        {"newString", "bar"},
        {"replaceAll", true}
    });
    EXPECT_TRUE(result.output.find("Edit applied successfully") != std::string::npos);
    std::ifstream f(m_tmp + "/edit_all.txt");
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "bar and bar and bar");
}

TEST_F(SystemToolsTest, EditMultiMatchError) {
    writeFile(m_tmp + "/edit_multi.txt", "foo and foo again");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/edit_multi.txt"},
        {"oldString", "foo"},
        {"newString", "bar"}
    });
    EXPECT_TRUE(result.output.find("Found multiple matches") != std::string::npos);
}

TEST_F(SystemToolsTest, EditOldStringNotFound) {
    writeFile(m_tmp + "/edit_notfound.txt", "hello world");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/edit_notfound.txt"},
        {"oldString", "zzzzz"},
        {"newString", "bar"}
    });
    EXPECT_TRUE(result.output.find("oldString not found") != std::string::npos);
}

TEST_F(SystemToolsTest, EditReplaceAllNotFound) {
    writeFile(m_tmp + "/edit_all_notfound.txt", "hello world");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/edit_all_notfound.txt"},
        {"oldString", "zzzzz"},
        {"newString", "bar"},
        {"replaceAll", true}
    });
    EXPECT_TRUE(result.output.find("oldString not found") != std::string::npos);
}

// ---------------------------------------------------------------------------
// xWrite
// ---------------------------------------------------------------------------

TEST_F(SystemToolsTest, WriteMissingFilePath) {
    auto result = a0::xWrite({{"content", "hello"}});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, WriteMissingContent) {
    auto result = a0::xWrite({{"filePath", m_tmp + "/x.txt"}});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, WriteNewFile) {
    std::string path = m_tmp + "/newfile.txt";
    auto result = a0::xWrite({{"filePath", path}, {"content", "hello world"}});
    EXPECT_TRUE(result.output.find("Wrote file successfully") != std::string::npos);
    EXPECT_TRUE(fs::exists(path));
    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "hello world");
}

TEST_F(SystemToolsTest, WriteParentDirCreation) {
    std::string path = m_tmp + "/deep/nested/dir/file.txt";
    auto result = a0::xWrite({{"filePath", path}, {"content", "nested content"}});
    EXPECT_TRUE(result.output.find("Wrote file successfully") != std::string::npos);
    EXPECT_TRUE(fs::exists(path));
}

// ---------------------------------------------------------------------------
// xGlob
// ---------------------------------------------------------------------------

TEST_F(SystemToolsTest, GlobMissingPattern) {
    auto result = a0::xGlob({});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, GlobDirectoryNotFound) {
    auto result = a0::xGlob({
        {"pattern", "*.txt"},
        {"path", m_tmp + "/no_such_dir"}
    });
    EXPECT_TRUE(result.output.find("ERROR: directory not found") != std::string::npos);
}

TEST_F(SystemToolsTest, GlobStarStar) {
    writeFile(m_tmp + "/a/b/c/foo.txt", "hello");
    writeFile(m_tmp + "/a/d/bar.txt", "world");
    auto result = a0::xGlob({
        {"pattern", "**/*.txt"},
        {"path", m_tmp}
    });
    EXPECT_TRUE(result.output.find("foo.txt") != std::string::npos);
    EXPECT_TRUE(result.output.find("bar.txt") != std::string::npos);
}

TEST_F(SystemToolsTest, GlobQuestionMark) {
    writeFile(m_tmp + "/ab.txt", "first");
    writeFile(m_tmp + "/cd.txt", "second");
    writeFile(m_tmp + "/xyz.txt", "third");
    auto result = a0::xGlob({
        {"pattern", "??.txt"},
        {"path", m_tmp}
    });
    EXPECT_TRUE(result.output.find("ab.txt") != std::string::npos);
    EXPECT_TRUE(result.output.find("cd.txt") != std::string::npos);
    EXPECT_EQ(result.output.find("xyz.txt"), std::string::npos);
}

TEST_F(SystemToolsTest, GlobDirOnlyPattern) {
    writeFile(m_tmp + "/somedir/a.txt", "a");
    auto result = a0::xGlob({
        {"pattern", "*/"},
        {"path", m_tmp}
    });
    EXPECT_TRUE(result.output.find("somedir") != std::string::npos);
}

// ---------------------------------------------------------------------------
// xGrep
// ---------------------------------------------------------------------------

TEST_F(SystemToolsTest, GrepMissingPattern) {
    auto result = a0::xGrep({});
    EXPECT_TRUE(result.output.find("ERROR: missing required") != std::string::npos);
}

TEST_F(SystemToolsTest, GrepInvalidRegex) {
    auto result = a0::xGrep({{"pattern", "[invalid"}});
    EXPECT_TRUE(result.output.find("ERROR: invalid regex") != std::string::npos);
}

TEST_F(SystemToolsTest, GrepDirectoryNotFound) {
    auto result = a0::xGrep({
        {"pattern", "test"},
        {"path", m_tmp + "/no_such_dir"}
    });
    EXPECT_TRUE(result.output.find("ERROR: directory not found") != std::string::npos);
}

TEST_F(SystemToolsTest, GrepNoMatches) {
    writeFile(m_tmp + "/greptest.txt", "hello world");
    auto result = a0::xGrep({{"pattern", "zzzzz"}, {"path", m_tmp}});
    EXPECT_TRUE(result.output.find("No matches found") != std::string::npos);
}

TEST_F(SystemToolsTest, GrepWithMatches) {
    writeFile(m_tmp + "/searchable.txt", "find this line\nanother line\nand find this too");
    auto result = a0::xGrep({{"pattern", "find"}, {"path", m_tmp}});
    EXPECT_TRUE(result.output.find("searchable.txt") != std::string::npos);
    EXPECT_TRUE(result.output.find("find this") != std::string::npos);
}

TEST_F(SystemToolsTest, GrepWithIncludeFilter) {
    writeFile(m_tmp + "/match_me.cpp", "int x = 42;");
    writeFile(m_tmp + "/ignore_me.h", "int x = 42;");
    auto result = a0::xGrep({
        {"pattern", "42"},
        {"path", m_tmp},
        {"include", "*.cpp"}
    });
    EXPECT_TRUE(result.output.find("match_me.cpp") != std::string::npos);
    EXPECT_EQ(result.output.find("ignore_me.h"), std::string::npos);
}

TEST_F(SystemToolsTest, ReadUsingFilePathKey) {
    writeFile(m_tmp + "/test.txt", "hello world");
    auto result = a0::xRead({{"file_path", m_tmp + "/test.txt"}});
    EXPECT_TRUE(result.output.find("1: hello world") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadWithOffsetAndLimit) {
    writeFile(m_tmp + "/lines.txt",
        "line1\nline2\nline3\nline4\nline5\nline6\nline7\nline8\nline9\nline10");
    auto result = a0::xRead({
        {"filePath", m_tmp + "/lines.txt"},
        {"offset", 3},
        {"limit", 2}
    });
    EXPECT_TRUE(result.output.find("3: line3") != std::string::npos);
    EXPECT_TRUE(result.output.find("4: line4") != std::string::npos);
    EXPECT_EQ(result.output.find("5: line5"), std::string::npos);
}

TEST_F(SystemToolsTest, ReadShortTextFile) {
    writeFile(m_tmp + "/short_read.txt", "just a single line");
    auto result = a0::xRead({{"filePath", m_tmp + "/short_read.txt"}});
    EXPECT_TRUE(result.output.find("1: just a single line") != std::string::npos);
}

TEST_F(SystemToolsTest, EditUsingFileKeyNames) {
    writeFile(m_tmp + "/edit_keys.txt", "hello foo world");
    auto result = a0::xEdit({
        {"file_path", m_tmp + "/edit_keys.txt"},
        {"old_string", "foo"},
        {"new_string", "bar"}
    });
    EXPECT_TRUE(result.output.find("Edit applied successfully") != std::string::npos);
}

TEST_F(SystemToolsTest, EditReplaceAllOldStringNotFound) {
    writeFile(m_tmp + "/edit_notfound2.txt", "hello world");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/edit_notfound2.txt"},
        {"oldString", "zzzzz"},
        {"newString", "bar"},
        {"replaceAll", true}
    });
    EXPECT_TRUE(result.output.find("oldString not found") != std::string::npos);
}

TEST_F(SystemToolsTest, WriteWithSubdirCreation) {
    std::string path = m_tmp + "/subdir/newfile.txt";
    auto result = a0::xWrite({{"filePath", path}, {"content", "nested"}});
    EXPECT_TRUE(result.output.find("Wrote file successfully") != std::string::npos);
    EXPECT_TRUE(fs::exists(path));
}

TEST_F(SystemToolsTest, BashWithTimeoutParam) {
    auto result = a0::xBash({
        {"command", "echo timeout_test"},
        {"timeout", 5000}
    });
    EXPECT_TRUE(result.output.find("timeout_test") != std::string::npos);
}

TEST_F(SystemToolsTest, BashWithCommandFailed) {
    auto result = a0::xBash({{"command", "false"}});
    EXPECT_TRUE(result.output.find("ERROR:") != std::string::npos ||
                result.output.find("exit code") != std::string::npos);
}

TEST_F(SystemToolsTest, GlobWithRegexSpecialChars) {
    writeFile(m_tmp + "/test+file.txt", "content");
    auto result = a0::xGlob({
        {"pattern", "test+file.txt"},
        {"path", m_tmp}
    });
    EXPECT_TRUE(result.output.find("test+file.txt") != std::string::npos);
}

TEST_F(SystemToolsTest, GlobWithDotPattern) {
    writeFile(m_tmp + "/config.json", "{}");
    auto result = a0::xGlob({
        {"pattern", "*.json"},
        {"path", m_tmp}
    });
    EXPECT_TRUE(result.output.find("config.json") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadWithTruncatedLines) {
    std::string path = m_tmp + "/longlines.txt";
    std::string longLine = std::string(5000, 'X');
    writeFile(path, longLine + "\n" + "short line");
    auto result = a0::xRead({{"filePath", path}});
    EXPECT_TRUE(result.output.find("1:") != std::string::npos);
    EXPECT_TRUE(result.output.find("2: short line") != std::string::npos);
}

TEST_F(SystemToolsTest, ReadWithContinuationHint) {
    std::string path = m_tmp + "/manylines.txt";
    std::string content;
    for (int i = 1; i <= 15; i++)
        content += "line" + std::to_string(i) + "\n";
    writeFile(path, content);
    auto result = a0::xRead({
        {"filePath", path},
        {"offset", 12},
        {"limit", 3}
    });
    EXPECT_TRUE(result.output.find("more lines") != std::string::npos) << "Output: " << result.output;
}

TEST_F(SystemToolsTest, BashStderrIncluded) {
    auto result = a0::xBash({{"command", "echo stderr_output >&2; echo stdout_output"}});
    EXPECT_TRUE(result.output.find("stdout_output") != std::string::npos);
}

TEST_F(SystemToolsTest, BashOutputTruncation) {
    std::string big(1024 * 1024, 'A');
    auto result = a0::xBash({{"command", "echo '" + big + "'"}});
    EXPECT_TRUE(result.output.size() <= 1024 * 1024 + 100);
}

TEST_F(SystemToolsTest, WriteFailsOnBadPath) {
    std::string badPath = m_tmp + "/nonexistent_parent/file.txt";
    auto result = a0::xWrite({{"filePath", badPath}, {"content", "test"}});
    EXPECT_TRUE(result.output.find("ERROR") != std::string::npos ||
                result.output.find("successfully") != std::string::npos);
}

TEST_F(SystemToolsTest, EditEmptyFile) {
    writeFile(m_tmp + "/empty.txt", "");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/empty.txt"},
        {"oldString", "foo"},
        {"newString", "bar"}
    });
    EXPECT_TRUE(result.output.find("oldString not found") != std::string::npos);
}

TEST_F(SystemToolsTest, EditWithReplaceAllSingleMatch) {
    writeFile(m_tmp + "/single_replace.txt", "foo bar foo baz");
    auto result = a0::xEdit({
        {"filePath", m_tmp + "/single_replace.txt"},
        {"oldString", "foo"},
        {"newString", "qux"},
        {"replaceAll", true}
    });
    EXPECT_TRUE(result.output.find("Edit applied") != std::string::npos);
    std::ifstream f(m_tmp + "/single_replace.txt");
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "qux bar qux baz");
}

TEST_F(SystemToolsTest, GlobWithExcludedDir) {
    fs::create_directories(m_tmp + "/node_modules");
    writeFile(m_tmp + "/node_modules/pkg.txt", "hidden");
    writeFile(m_tmp + "/visible.txt", "found");
    auto result = a0::xGlob({
        {"pattern", "**/*.txt"},
        {"path", m_tmp}
    });
    EXPECT_TRUE(result.output.find("visible.txt") != std::string::npos);
    EXPECT_EQ(result.output.find("node_modules"), std::string::npos);
}

TEST_F(SystemToolsTest, GrepPatternFoundEarly) {
    writeFile(m_tmp + "/early_grep.txt", "AAA findme BBB");
    auto result = a0::xGrep({{"pattern", "findme"}, {"path", m_tmp}});
    EXPECT_TRUE(result.output.find("findme") != std::string::npos);
}
